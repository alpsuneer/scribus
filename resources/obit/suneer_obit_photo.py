#!/usr/bin/env python3
"""Obituary photo worker for the Scribus News Browser (Obituary layout model).

Reads one job file (JSON) and writes, for every item, a square JPEG with
  1. the background removed (rembg),
  2. one uniform background tone,
  3. a face-centred crop: face centred horizontally, eyes at ~40 % of the
     height, the top of the head ~5 % below the top edge, same face size on
     every photo. Without a face: centre crop, status "noface".
Nothing is written over the source. Results are cached: an output whose
sidecar (<out>.json) matches the source bytes and the parameters is kept.

Job file:
  {"size": 600, "quality": 95, "bg": [r, g, b], "model": "u2net_human_seg",
   "eyeY": 0.40, "faceH": 0.50, "items": [{"id": "...", "src": "...", "out": "..."}]}
Progress on stdout, one JSON line per item, flushed:
  PROGRESS {"i":1,"n":12,"id":"...","status":"ok|noface|cached|fail","out":"...","message":"..."}
"""
import hashlib
import json
import os
import sys
import time

PARAM_KEYS = ("size", "quality", "bg", "model", "eyeY", "faceH", "matting")


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


def process(item, job, session, i, n):
    from PIL import Image, ImageOps
    import numpy as np
    from rembg import remove

    src, out = item["src"], item["out"]
    size = int(job.get("size", 600))
    quality = int(job.get("quality", 95))
    bg = tuple(int(c) for c in job.get("bg", [190, 190, 190]))[:3]
    eye_y = float(job.get("eyeY", 0.40))
    face_h = float(job.get("faceH", 0.50))

    img = Image.open(src)
    img = ImageOps.exif_transpose(img).convert("RGB")
    rgb = np.asarray(img)
    bgr = rgb[:, :, ::-1].copy()
    face = detect_face(bgr)

    # 1. background removal on the whole picture (the crop is padded with the
    #    background tone, so a tight source still fills the square).
    cut = remove(img, session=session, alpha_matting=bool(job.get("matting", False)))
    cut = cut.convert("RGBA")

    # 2. one uniform tone behind the subject
    flat = Image.new("RGBA", cut.size, bg + (255,))
    flat.alpha_composite(cut)
    flat = flat.convert("RGB")

    # 3. face-centred crop in source pixels
    W, H = flat.size
    if face is not None:
        fx, fy, fw, fh = face
        side = fh / face_h                      # source pixels per output square
        cx = fx + fw / 2.0
        eyes = fy + 0.40 * fh                   # eyes sit ~40 % down a Haar box
        top = eyes - eye_y * side
        left = cx - side / 2.0
        status = "ok"
    else:
        side = float(min(W, H))
        left = (W - side) / 2.0
        top = 0.0 if H > W else (H - side) / 2.0   # portraits: keep the head
        status = "noface"
    box = (int(round(left)), int(round(top)), int(round(left + side)), int(round(top + side)))
    canvas = Image.new("RGB", (box[2] - box[0], box[3] - box[1]), bg)
    canvas.paste(flat.crop((max(0, box[0]), max(0, box[1]), min(W, box[2]), min(H, box[3]))),
                 (max(0, -box[0]), max(0, -box[1])))
    canvas = canvas.resize((size, size), Image.LANCZOS)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    tmp = out + ".part"
    canvas.save(tmp, "JPEG", quality=quality, subsampling=0, dpi=(503, 503))
    os.replace(tmp, out)
    return status, face


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
                log(i, n, item_id, "cached", out, meta.get("status", "ok"))
                continue
            if session is None:
                from rembg import new_session
                session = new_session(job.get("model", "u2net_human_seg"))
            t0 = time.time()
            status, face = process(item, job, session, i, n)
            with open(out + ".json", "w", encoding="utf-8") as f:
                json.dump({"src": src, "srcSha1": src_sha, "params": params_of(job),
                           "status": status, "face": list(face) if face else None,
                           "seconds": round(time.time() - t0, 2)}, f)
            log(i, n, item_id, status, out, "%.1fs" % (time.time() - t0))
        except Exception as e:  # never stop the batch for one picture
            log(i, n, item_id, "fail", out, "%s: %s" % (type(e).__name__, e))
    sys.stdout.write("DONE\n")
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
