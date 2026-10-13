#!/usr/bin/env python3
"""Obituary photo worker for the Scribus News Browser (Obituary layout model).

Reads one job file (JSON) and writes, for every item, a square JPEG with
  1. the background removed (rembg),
  2. one uniform background tone,
  3. same-tone portrait: white balance, auto levels, gentle contrast, one
     target face luminance, slight unsharp (strength 0-100),
  4. a face-fill crop: top of the head headTop below the frame top, face
     (hairline-chin) = faceH of the frame height, centred, shoulders cut at
     the bottom. Without a face: centre crop, status "noface"; source too
     small for 300 ppi: largest crop keeping 300 ppi, status "lowres".
Nothing is written over the source. Results are cached: an output whose
sidecar (<out>.json) matches the source bytes and the parameters is kept.

Job file:
  {"size": 600, "outH": 600, "quality": 95, "bg": [r, g, b], "model": "u2net_human_seg",
   "faceH": 0.55, "headTop": 0.03, "toneStrength": 60, "minPx": 358,
   "items": [{"id": "...", "src": "...", "out": "..."}]}
  faceH = (hairline..chin) / frame height; headTop = top of the head below the
  frame top; toneStrength 0-100 blends the tone matching; minPx = source
  pixels the crop needs for 300 ppi (smaller sources are flagged "lowres").
Progress on stdout, one JSON line per item, flushed:
  PROGRESS {"i":1,"n":12,"id":"...","status":"ok|noface|cached|fail","out":"...","message":"..."}
"""
import hashlib
import json
import os
import sys
import time

PARAM_KEYS = ("size", "outH", "quality", "bg", "model", "eyeY", "faceH", "headTop", "toneStrength", "minPx", "mm", "matting")


def log(i, n, item_id, status, out, msg=""):
    rec = {"i": i, "n": n, "id": item_id, "status": status, "out": out, "message": msg.replace("\n", " ")}
    sys.stdout.write("PROGRESS " + json.dumps(rec, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def params_of(job):
    return {k: job.get(k) for k in PARAM_KEYS}


def cached(item, job, src_sha):
    out = item["out"]
    side = out + ".json"
    if not (os.path.isfile(out) and os.path.isfile(side)):
        return None
    try:
        with open(side, "r", encoding="utf-8") as f:
            meta = json.load(f)
    except Exception:
        return None
    if meta.get("srcSha1") != src_sha or meta.get("params") != params_of(job):
        return None
    return meta


def detect_face(bgr):
    """Largest frontal face as (x, y, w, h) in pixels, or None."""
    import cv2
    gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    gray = cv2.equalizeHist(gray)
    h, w = gray.shape[:2]
    # Small sources are upscaled so the cascade still sees a face.
    scale = 1.0
    if min(h, w) < 400:
        scale = 400.0 / min(h, w)
        gray = cv2.resize(gray, (int(w * scale), int(h * scale)), interpolation=cv2.INTER_CUBIC)
    best = None
    for name, neighbours in (("haarcascade_frontalface_default.xml", 5),
                             ("haarcascade_frontalface_alt2.xml", 3)):
        cc = cv2.CascadeClassifier(cv2.data.haarcascades + name)
        if cc.empty():
            continue
        faces = cc.detectMultiScale(gray, scaleFactor=1.08, minNeighbors=neighbours,
                                    minSize=(int(min(gray.shape[:2]) / 12),) * 2)
        for (x, y, fw, fh) in faces:
            if best is None or fw * fh > best[2] * best[3]:
                best = (x, y, fw, fh)
        if best is not None:
            break
    if best is None:
        return None
    x, y, fw, fh = best
    return (x / scale, y / scale, fw / scale, fh / scale)


def tone_match(rgb, face, strength):
    """Same-tone portrait: grey-world white balance, 1 % auto levels, gentle
    contrast, the face region brought to one target mean luminance, slight
    unsharp mask. rgb: float32 HxWx3 in 0..255. strength 0..1 blends."""
    import numpy as np
    import cv2
    if strength <= 0:
        return rgb
    src = rgb.copy()
    out = rgb.copy()
    # 1. white balance (grey world on the mid-tones, so no colour cast)
    mid = (out.mean(axis=2) > 40) & (out.mean(axis=2) < 220)
    if mid.sum() > 100:
        means = np.array([out[:, :, c][mid].mean() for c in range(3)])
        gain = means.mean() / np.maximum(means, 1.0)
        gain = np.clip(gain, 0.8, 1.25)          # gentle: skin must stay natural
        out = out * gain.reshape(1, 1, 3)
    # 2. auto levels, 1 % clip, on luminance
    lum = 0.299 * out[:, :, 0] + 0.587 * out[:, :, 1] + 0.114 * out[:, :, 2]
    lo, hi = np.percentile(lum, 1.0), np.percentile(lum, 99.0)
    if hi - lo > 20:
        out = (out - lo) * (255.0 / (hi - lo))
    # 3. gentle contrast (S-curve around the middle)
    out = np.clip(out, 0, 255)
    out = 255.0 * (0.5 + 0.5 * np.tanh(1.15 * (out / 255.0 - 0.5) * 2.0) / np.tanh(1.15))
    # 4. same mean luminance on the face (target 150/255), via gamma
    if face is not None:
        fx, fy, fw, fh = [int(v) for v in face]
        H, W = out.shape[:2]
        reg = out[max(0, fy):min(H, fy + fh), max(0, fx):min(W, fx + fw)]
        if reg.size > 0:
            flum = (0.299 * reg[:, :, 0] + 0.587 * reg[:, :, 1] + 0.114 * reg[:, :, 2]).mean()
            target = 150.0
            if flum > 5:
                gamma = np.log(target / 255.0) / np.log(max(flum, 1.0) / 255.0)
                gamma = float(np.clip(gamma, 0.7, 1.4))
                out = 255.0 * np.power(np.clip(out / 255.0, 0, 1), gamma)
    # 5. slight unsharp mask
    blur = cv2.GaussianBlur(out.astype(np.float32), (0, 0), 1.2)
    out = out + 0.5 * (out - blur)
    out = np.clip(out, 0, 255)
    return src * (1.0 - strength) + out * strength


def process(item, job, session, i, n):
    from PIL import Image, ImageOps
    import numpy as np
    from rembg import remove

    src, out = item["src"], item["out"]
    size = int(job.get("size", 600))                  # output width px
    out_h = int(job.get("outH", size))                # output height px (portrait frame: taller)
    quality = int(job.get("quality", 95))
    bg = tuple(int(c) for c in job.get("bg", [194, 198, 199]))[:3]   # "Obit BG" #C2C6C7
    face_h = float(job.get("faceH", 0.55))            # hairline-chin / frame height
    head_top = float(job.get("headTop", 0.03))        # top of the head below the frame top
    strength = float(job.get("toneStrength", 60)) / 100.0
    min_px = int(job.get("minPx", 0))                 # source px the crop must have for 300 ppi

    img = Image.open(src)
    img = ImageOps.exif_transpose(img).convert("RGB")
    rgb = np.asarray(img)
    bgr = rgb[:, :, ::-1].copy()
    face = detect_face(bgr)

    # 1. background removal on the whole picture (the crop is padded with the
    #    background tone, so a tight source still fills the frame).
    cut = remove(img, session=session, alpha_matting=bool(job.get("matting", False)))
    cut = cut.convert("RGBA")
    alpha = np.asarray(cut)[:, :, 3]

    # 2. tone matching of the person, then the uniform tone behind
    person = tone_match(np.asarray(cut.convert("RGB")).astype(np.float32), face, strength)
    toned = Image.fromarray(person.astype(np.uint8)).convert("RGBA")
    toned.putalpha(Image.fromarray(alpha))
    flat = Image.new("RGBA", cut.size, bg + (255,))
    flat.alpha_composite(toned)
    flat = flat.convert("RGB")

    # 3. face fill crop in source pixels
    W, H = flat.size
    status = "ok"
    info = {"srcW": W, "srcH": H}
    if face is not None:
        fx, fy, fw, fh = face
        # Haar box: eyebrows..chin. Hairline ~0.1 h above it, chin at its bottom.
        hairline = fy - 0.10 * fh
        chin = fy + 1.0 * fh
        # top of the head from the cut-out itself (highest opaque row over the face)
        cols = alpha[:, max(0, int(fx)):min(W, int(fx + fw))]
        rows = np.where((cols > 128).sum(axis=1) > max(2, int(0.2 * fw)))[0]
        top_of_head = float(rows[0]) if rows.size and rows[0] < hairline else hairline - 0.25 * fh
        frame_h_src = (chin - hairline) / face_h            # frame height in source px
        frame_w_src = frame_h_src * size / float(out_h)
        cx = fx + fw / 2.0
        top = top_of_head - head_top * frame_h_src
        left = cx - frame_w_src / 2.0
        info["wantedPx"] = int(round(frame_w_src))
        if min_px > 0 and frame_w_src < min_px:
            # Too small for 300 ppi: the largest crop that keeps 300 ppi, flagged.
            scale = min_px / frame_w_src
            frame_w_src, frame_h_src = min_px, frame_h_src * scale
            left = cx - frame_w_src / 2.0
            top = top_of_head - head_top * frame_h_src
            status = "lowres"
    else:
        frame_w_src = float(min(W, H * size / float(out_h)))
        frame_h_src = frame_w_src * out_h / float(size)
        left = (W - frame_w_src) / 2.0
        top = 0.0 if H > W else (H - frame_h_src) / 2.0   # portraits: keep the head
        status = "noface"
    box = (int(round(left)), int(round(top)), int(round(left + frame_w_src)), int(round(top + frame_h_src)))
    canvas = Image.new("RGB", (box[2] - box[0], box[3] - box[1]), bg)
    canvas.paste(flat.crop((max(0, box[0]), max(0, box[1]), min(W, box[2]), min(H, box[3]))),
                 (max(0, -box[0]), max(0, -box[1])))
    canvas = canvas.resize((size, out_h), Image.LANCZOS)
    mm = float(job.get("mm", 30.3))
    # effective ppi of the printed frame = source pixels across the crop / frame width in inches
    info["cropPx"] = int(round(frame_w_src))
    info["ppi"] = int(round(info.get("wantedPx", frame_w_src) / (mm / 25.4)))
    info["status"] = status
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    tmp = out + ".part"
    if out.lower().endswith(".png"):
        # Lossless: the background tone is exactly the requested RGB in every file.
        canvas.save(tmp, "PNG", dpi=(503, 503), optimize=True)
    else:
        canvas.save(tmp, "JPEG", quality=quality, subsampling=0, dpi=(503, 503))
    os.replace(tmp, out)
    return status, face, info


def main():
    if len(sys.argv) != 3 or sys.argv[1] != "--jobs":
        sys.stderr.write("usage: suneer_obit_photo.py --jobs <job.json>\n")
        return 2
    with open(sys.argv[2], "r", encoding="utf-8") as f:
        job = json.load(f)
    items = job.get("items", [])
    n = len(items)
    session = None
    for i, item in enumerate(items, 1):
        item_id, src, out = item.get("id", "?"), item.get("src", ""), item.get("out", "")
        try:
            if not os.path.isfile(src):
                log(i, n, item_id, "fail", out, "source missing: " + src)
                continue
            src_sha = sha1_file(src)
            meta = cached(item, job, src_sha)
            if meta is not None:
                log(i, n, item_id, "cached", out, "%s|%s ppi (%sx%s px)" % (meta.get("status", "ok"), meta.get("ppi", "?"), meta.get("srcW", "?"), meta.get("srcH", "?")))
                continue
            if session is None:
                from rembg import new_session
                session = new_session(job.get("model", "u2net_human_seg"))
            t0 = time.time()
            status, face, info = process(item, job, session, i, n)
            with open(out + ".json", "w", encoding="utf-8") as f:
                json.dump({"src": src, "srcSha1": src_sha, "params": params_of(job),
                           "status": status, "face": list(face) if face else None,
                           "ppi": info.get("ppi"), "srcW": info.get("srcW"), "srcH": info.get("srcH"),
                           "seconds": round(time.time() - t0, 2)}, f)
            log(i, n, item_id, status, out, "%d ppi (%dx%d px)" % (info.get("ppi", 0), info.get("srcW", 0), info.get("srcH", 0)))
        except Exception as e:  # never stop the batch for one picture
            log(i, n, item_id, "fail", out, "%s: %s" % (type(e).__name__, e))
    sys.stdout.write("DONE\n")
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
