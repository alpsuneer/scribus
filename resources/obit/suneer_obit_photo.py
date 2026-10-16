#!/usr/bin/env python3
"""Obituary photo worker for the Scribus News Browser (Obituary layout model).

Reads one job file (JSON) and writes, for every item, a square JPEG with
  1. the background removed (rembg),
  2. one uniform background tone,
  3. same-tone portrait: white balance, auto levels, gentle contrast, one
     target face luminance, slight unsharp (strength 0-100),
  4. a tight face crop in Haar frontal-face box terms (faceW > 0): the face
     box is faceW (0.60) of the frame width, centred, its top faceTop (0.18)
     of the frame width below the frame top - hair almost at the top edge,
     only neck and collar below the chin. (faceW = 0: the older hairline-chin
     rule with faceH / headTop.) Without a face: centre crop, status
     "noface"; source too small for 300 ppi: largest crop keeping 300 ppi,
     status "lowres" (ppi in the message).
  Background removal is checked: when the cut-out still fills the top
  corners of the crop, the next model is tried (model, then the others of
  bgModels); when every model fails the status is "bg". The alpha edge is
  eroded by 1 px (no halo).
Nothing is written over the source. Results are cached: an output whose
sidecar (<out>.json) matches the source bytes and the parameters is kept.

Job file:
  {"size": 600, "outH": 600, "quality": 95, "bg": [r, g, b], "model": "u2net_human_seg",
   "faceW": 0.60, "faceTop": 0.18, "faceH": 0.55, "headTop": 0.03, "toneStrength": 60, "minPx": 358,
   "bgModels": ["u2net_human_seg", "isnet-general-use", "u2net"],
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

PARAM_KEYS = ("size", "outH", "quality", "bg", "model", "eyeY", "faceH", "headTop", "faceW", "faceTop",
              "toneStrength", "minPx", "mm", "matting", "bgModels")


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


def corner_opacity(alpha, box, chin=None):
    """Share of non-background pixels in the two top corners of the crop box
    (squares of 12 % of the crop width) and in the two side strips (8 % of
    the crop width, from the crop top down to the chin). A person never
    fills those in a tight portrait crop; a kept background does. \a alpha
    is a 0..255 map of "not background" (alpha, or the difference of the
    composite from the tone, so a half-transparent wall counts too).
    Returns the worst of the four."""
    import numpy as np
    H, W = alpha.shape[:2]
    x0, y0, x1, y1 = box
    cw = x1 - x0
    side = max(4, int(round(0.12 * cw)))
    strip = max(3, int(round(0.08 * cw)))
    yc = int(chin) if chin is not None else y0 + int(0.6 * cw)
    regions = [(x0, y0, x0 + side, y0 + side), (x1 - side, y0, x1, y0 + side),
               (x0, y0, x0 + strip, yc), (x1 - strip, y0, x1, yc)]
    vals = []
    for xa, ya, xb, yb in regions:
        xa, xb = max(0, xa), min(W, xb)
        ya, yb = max(0, ya), min(H, yb)
        if xb - xa < 2 or yb - ya < 2:
            vals.append(0.0)        # outside the source: padded with the tone, so clean
            continue
        reg = alpha[ya:yb, xa:xb]
        vals.append(float((reg > 128).mean()))
    return max(vals)


def clean_edge(alpha):
    """1 px erosion of the opaque region, then a half-pixel soft edge: no
    halo of the old background along the outline."""
    import numpy as np
    import cv2
    mask = (alpha > 127).astype(np.uint8)
    mask = cv2.erode(mask, np.ones((3, 3), np.uint8), iterations=1)
    a = np.where(mask > 0, alpha, 0).astype(np.float32)
    a = cv2.GaussianBlur(a, (0, 0), 0.5)
    return np.clip(a, 0, 255).astype(np.uint8)


def crop_box(face, alpha, W, H, job, size, out_h, min_px):
    """The crop in source pixels: (left, top, w, h), status, info."""
    import numpy as np
    face_w = float(job.get("faceW", 0.0))            # Haar box width / frame width
    face_top = float(job.get("faceTop", 0.18))       # Haar box top below the frame top, in frame widths
    face_h = float(job.get("faceH", 0.55))
    head_top = float(job.get("headTop", 0.03))
    status = "ok"
    info = {}
    if face is not None:
        fx, fy, fw, fh = face
        cx = fx + fw / 2.0
        if face_w > 0:
            frame_w_src = fw / face_w
            frame_h_src = frame_w_src * out_h / float(size)
            top = fy - face_top * frame_w_src
        else:
            hairline = fy - 0.10 * fh
            chin = fy + 1.0 * fh
            cols = alpha[:, max(0, int(fx)):min(W, int(fx + fw))]
            rows = np.where((cols > 128).sum(axis=1) > max(2, int(0.2 * fw)))[0]
            top_of_head = float(rows[0]) if rows.size and rows[0] < hairline else hairline - 0.25 * fh
            frame_h_src = (chin - hairline) / face_h
            frame_w_src = frame_h_src * size / float(out_h)
            top = top_of_head - head_top * frame_h_src
        left = cx - frame_w_src / 2.0
        info["wantedPx"] = int(round(frame_w_src))
        if min_px > 0 and frame_w_src < min_px:
            # Too small for 300 ppi: the largest crop that keeps 300 ppi,
            # face still centred and at the same relative top; flagged.
            scale = min_px / frame_w_src
            frame_w_src, frame_h_src = min_px, frame_h_src * scale
            left = cx - frame_w_src / 2.0
            top = fy - (face_top if face_w > 0 else 0.18) * frame_w_src
            status = "lowres"
        info["faceWidthPct"] = int(round(100.0 * fw / frame_w_src))
        info["faceTopPct"] = int(round(100.0 * (fy - top) / frame_w_src))
        info["faceCentrePct"] = int(round(100.0 * (cx - left) / frame_w_src))
    else:
        frame_w_src = float(min(W, H * size / float(out_h)))
        frame_h_src = frame_w_src * out_h / float(size)
        left = (W - frame_w_src) / 2.0
        top = 0.0 if H > W else (H - frame_h_src) / 2.0   # portraits: keep the head
        status = "noface"
    return (left, top, frame_w_src, frame_h_src), status, info


def process(item, job, session, i, n):
    from PIL import Image, ImageOps
    import numpy as np
    from rembg import remove, new_session

    src, out = item["src"], item["out"]
    size = int(job.get("size", 600))                  # output width px
    out_h = int(job.get("outH", size))                # output height px (portrait frame: taller)
    quality = int(job.get("quality", 95))
    bg = tuple(int(c) for c in job.get("bg", [194, 198, 199]))[:3]   # "Obit BG" #C2C6C7
    strength = float(job.get("toneStrength", 60)) / 100.0
    min_px = int(job.get("minPx", 0))                 # source px the crop must have for 300 ppi

    img = Image.open(src)
    img = ImageOps.exif_transpose(img).convert("RGB")
    rgb = np.asarray(img)
    bgr = rgb[:, :, ::-1].copy()
    face = detect_face(bgr)

    W, H = img.size
    # 1. background removal on the whole picture (the crop is padded with the
    #    background tone, so a tight source still fills the frame). The result
    #    is checked on the top corners of the crop; a kept background means
    #    the next model is tried; the cleanest result wins.
    first_box, _, _ = crop_box(face, np.zeros((H, W), np.uint8), W, H, job, size, out_h, min_px)
    bx = (int(round(first_box[0])), int(round(first_box[1])), int(round(first_box[0] + first_box[2])), int(round(first_box[1] + first_box[3])))
    # background policy: "auto" (AI), "ask" (AI, but a damaged result is not
    # used), "manual" (no AI at all; the mask is made by hand in Edit photo)
    policy = str(job.get("bgPolicy", "auto"))
    if policy == "manual":
        models = []
        best = (0.0, img.convert("RGBA"), "manual")
    else:
        models = [job.get("model", "u2net_human_seg")] + [m for m in job.get("bgModels", ["isnet-general-use", "u2net"]) if m != job.get("model", "u2net_human_seg")]
        best = None     # (opacity, cut, model)
    bg_errors = []
    for mi, mname in enumerate(models):
        try:
            sess = session if (mi == 0 and session is not None) else new_session(mname)
            c = remove(img, session=sess, alpha_matting=bool(job.get("matting", False))).convert("RGBA")
        except Exception as e:  # a missing model file or a runtime error: try the next
            bg_errors.append("%s: %s" % (mname, e))
            continue
        # "not background" = the composite over the tone differs from the tone
        # (a half-transparent wall shows and must count) or the alpha is set.
        a = np.asarray(c)[:, :, 3].astype(np.float32)
        rgbc = np.asarray(c)[:, :, :3].astype(np.float32)
        comp = rgbc * (a / 255.0)[:, :, None] + np.array(bg, np.float32) * (1.0 - a / 255.0)[:, :, None]
        diff = np.abs(comp - np.array(bg, np.float32)).max(axis=2)
        notbg = np.where((diff > 24) | (a > 128), 255, 0).astype(np.uint8)
        opq = corner_opacity(notbg, bx, chin=(face[1] + face[3]) if face is not None else None)
        if best is None or opq < best[0]:
            best = (opq, c, mname)
        if opq <= 0.25:
            break
    if best is None:
        raise RuntimeError("background removal failed: " + "; ".join(bg_errors))
    bg_opacity, cut, bg_model = best
    bg_ok = bg_opacity <= 0.5
    alpha = clean_edge(np.asarray(cut)[:, :, 3])
    # "Protect" / "Exclude" areas from the Edit photo dialog (Grayscale PNG,
    # source size: 255 always kept, 0 always removed, 128 untouched): they
    # win over the model, so a re-process never cuts a protected hand again.
    pmask = item.get("protect") or ""
    if pmask and os.path.isfile(pmask):
        try:
            pm = np.asarray(Image.open(pmask).convert("L"))
            if pm.shape == alpha.shape:
                alpha = np.where(pm > 128, 255, np.where(pm < 128, 0, alpha)).astype(np.uint8)
        except Exception:
            pass
    # "Ask when quality is low": the AI keeps too little of the crop, or cuts
    # through the face box -> the damaged result is not used (no removal),
    # the photo is flagged "check BG".
    ask_msg = ""
    if policy == "ask":
        ah, aw = alpha.shape[:2]
        reg = alpha[max(0, bx[1]):min(ah, bx[3]), max(0, bx[0]):min(aw, bx[2])]
        keep = float((reg > 128).mean()) if reg.size else 1.0
        face_keep = 1.0
        if face is not None:
            fx, fy, fw, fh = [int(v) for v in face]
            fr = alpha[max(0, fy):min(ah, fy + fh), max(0, fx):min(aw, fx + fw)]
            face_keep = float((fr > 128).mean()) if fr.size else 1.0
        if keep < 0.25 or face_keep < 0.6:
            ask_msg = "AI kept %d%% of the crop, %d%% of the face - not used" % (round(100 * keep), round(100 * face_keep))
            alpha = np.full_like(alpha, 255)

    # 2. tone matching of the person, then the uniform tone behind
    person = tone_match(np.asarray(cut.convert("RGB")).astype(np.float32), face, strength)
    toned = Image.fromarray(person.astype(np.uint8)).convert("RGBA")
    toned.putalpha(Image.fromarray(alpha))
    flat = Image.new("RGBA", cut.size, bg + (255,))
    flat.alpha_composite(toned)
    flat = flat.convert("RGB")

    # 3. the crop in source pixels (same rule as the first_box above)
    W, H = flat.size
    (left, top, frame_w_src, frame_h_src), status, cinfo = crop_box(face, alpha, W, H, job, size, out_h, min_px)
    info = {"srcW": W, "srcH": H, "bgModel": bg_model, "bgOpacityPct": int(round(100 * bg_opacity)), "bgOk": bg_ok}
    info.update(cinfo)
    if not bg_ok and status == "ok":
        status = "bg"          # every model kept the background: check by hand
    if ask_msg:
        status = "bg"
        info["askMsg"] = ask_msg
        info["bgOk"] = False
    if policy == "manual":
        info["manual"] = True
    box = (int(round(left)), int(round(top)), int(round(left + frame_w_src)), int(round(top + frame_h_src)))
    canvas = Image.new("RGB", (box[2] - box[0], box[3] - box[1]), bg)
    canvas.paste(flat.crop((max(0, box[0]), max(0, box[1]), min(W, box[2]), min(H, box[3]))),
                 (max(0, -box[0]), max(0, -box[1])))
    canvas = canvas.resize((size, out_h), Image.LANCZOS)
    # The same crop as a transparent cut-out: a later "Photo background
    # colour" only composites it over the new tone, no rembg run again.
    cutout = Image.new("RGBA", (box[2] - box[0], box[3] - box[1]), (0, 0, 0, 0))
    cutout.paste(toned.crop((max(0, box[0]), max(0, box[1]), min(W, box[2]), min(H, box[3]))),
                 (max(0, -box[0]), max(0, -box[1])))
    cutout = cutout.resize((size, out_h), Image.LANCZOS)
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
        try:
            cutout.save(out[:-4] + ".cut.png", "PNG", dpi=(503, 503))
        except Exception:
            pass
    else:
        canvas.save(tmp, "JPEG", quality=quality, subsampling=0, dpi=(503, 503))
    os.replace(tmp, out)
    return status, face, info


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--face":
        # "Edit photo" dialog: the largest frontal face of one picture, JSON on stdout.
        try:
            from PIL import Image, ImageOps
            import numpy as np
            img = ImageOps.exif_transpose(Image.open(sys.argv[2])).convert("RGB")
            face = detect_face(np.asarray(img)[:, :, ::-1].copy())
            sys.stdout.write(json.dumps({"face": list(face) if face else None, "w": img.width, "h": img.height}) + "\n")
            return 0
        except Exception as e:
            sys.stdout.write(json.dumps({"face": None, "error": "%s: %s" % (type(e).__name__, e)}) + "\n")
            return 1
    if len(sys.argv) != 3 or sys.argv[1] != "--jobs":
        sys.stderr.write("usage: suneer_obit_photo.py --jobs <job.json> | --face <image>\n")
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
                           "faceWidthPct": info.get("faceWidthPct"), "faceTopPct": info.get("faceTopPct"),
                           "faceCentrePct": info.get("faceCentrePct"), "bgModel": info.get("bgModel"),
                           "bgOpacityPct": info.get("bgOpacityPct"), "bgOk": info.get("bgOk"),
                           "seconds": round(time.time() - t0, 2)}, f)
            msg = "%d ppi (%dx%d px)" % (info.get("ppi", 0), info.get("srcW", 0), info.get("srcH", 0))
            if info.get("askMsg"):
                msg += "; " + info.get("askMsg")
            elif not info.get("bgOk", True):
                msg += "; bg? background kept (%s, %d%% at the edges)" % (info.get("bgModel"), info.get("bgOpacityPct", 0))
            log(i, n, item_id, status, out, msg)
        except Exception as e:  # never stop the batch for one picture
            log(i, n, item_id, "fail", out, "%s: %s" % (type(e).__name__, e))
    sys.stdout.write("DONE\n")
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
