# Scribus Malayalam DTP — Kasm Workspaces Edition

Browser-accessible **Scribus 1.7.3** (Malayalam DTP build) with the
Photoshop-style image editor and **SAM AI Smart Select**, packaged for
[Kasm Workspaces](https://www.kasmweb.com/). The GUI is streamed to the browser
via KasmVNC on an XFCE desktop; nothing is installed on the end-user's machine.

---

## What's in the image

| Component | Detail |
|-----------|--------|
| Base | `kasmweb/core-debian-trixie` (Debian 13 + KasmVNC + XFCE) |
| Scribus | Built from source, `Release`, installed to `/usr/local` |
| Image editor | Non-destructive effect stack, selection framework, refine-edges brush |
| SAM Smart Select | ONNX Runtime from Debian (`libonnxruntime-dev`), `-DWITH_SAM=ON` |
| Fonts | SMC Malayalam suite, Lohit, Samyak, Noto |
| Input method | fcitx5 + m17n, **including the custom Deshabhimani + Praja tables** |
| Auto-launch | Scribus starts automatically once the desktop is ready |
| User | Non-root `kasm-user` (UID 1000), Kasm default-profile conventions |

### Why the Debian base

The fork is developed on Debian 13 (trixie). Verified inside the base image,
every dependency resolves to the **identical version the host builds against**:

| Package | Base image | Development host |
|---|---|---|
| `qt6-base-dev` | 6.8.2+dfsg-9+deb13u2 | 6.8.2+dfsg-9+deb13u2 |
| `libonnxruntime-dev` | 1.21.0+dfsg-1 | 1.21.0+dfsg-1 |
| `libpoppler-cpp-dev` | 25.03.0-5+deb13u4 | 25.03.0-5+deb13u4 |
| `libpodofo-dev` | 0.9.8+dfsg-3.2 | 0.9.8+dfsg-3.2 |
| `libjxl-dev` | 0.11.2-0.1~deb13u2 | 0.11.2-0.1~deb13u2 |

Ubuntu 24.04 (noble) was rejected: it ships **Qt 6.4.2** and has **no
`libonnxruntime` package at all**. `QT_MIN_VERSION` is 6.4.0 so noble would
configure, but it would mean compiling this fork against a Qt it has never been
built with, plus hand-installing Microsoft's ONNX tarball.

---

## Quick Deploy

### Prerequisites
- A running Kasm Workspaces deployment (Admin access)
- A Docker registry reachable by your Kasm agents (or load the image on each agent)
- **~10 GB free** on the Docker data root to build (the base image alone is 4.56 GB)

### Steps
1. **Build the image** (run from the repository root, *not* from `kasm/`):
   ```bash
   docker build -f kasm/Dockerfile.kasm -t scribus-mdtp:kasm .
   ```
   To bake the SAM models in:
   ```bash
   docker build -f kasm/Dockerfile.kasm \
       --build-arg INCLUDE_SAM_MODELS=true -t scribus-mdtp:kasm .
   ```

2. **Push to your registry:**
   ```bash
   docker tag scribus-mdtp:kasm <registry>/scribus-mdtp:kasm
   docker push <registry>/scribus-mdtp:kasm
   ```

3. **Register the workspace** in Kasm Admin → *Workspaces → Add Workspace*,
   using `kasm/kasm-workspace-config.json` as a reference.

4. **Users launch it** from the Kasm dashboard — Scribus opens automatically.

---

## The three conventions this image gets right

These are the failure modes the previous draft had. They are subtle, and two of
them only show up *after* the demo, in production.

### 1. Build-time `HOME` is `/home/kasm-default-profile`

Kasm's `/dockerstartup/kasm_default_profile.sh` copies
`/home/kasm-default-profile` into `$HOME` at session start (gated on
`$HOME/.bashrc` not existing). `/home/kasm-user` is the *runtime* profile.

Anything baked directly into `/home/kasm-user` is **destroyed the moment
persistent profiles are enabled**, because the user's volume mounts over that
path. It works in a laptop demo and silently breaks in cloud production.

The Dockerfile therefore sets `ENV HOME=/home/kasm-default-profile` for the
customisation steps and resets it to `/home/kasm-user` at the end.

### 2. The custom m17n tables are installed

`ml-deshabhimani.mim` is the newsroom's **production default** keyboard
(`fcitx5/profile: DefaultIM=m17n_ml_deshabhimani`), and `te-praja-sr.mim` is the
Telugu layout. **Neither ships in `m17n-db`** — both are hand-installed on the
host and are not dpkg-owned. Installing `fcitx5-m17n` alone yields the stock
tables and silently loses the layout the operators actually type on.

Both live in `kasm/ime/` and are copied to `/usr/share/m17n/`. The baked fcitx5
profile sets Deshabhimani as the default IM.

> If you update either table on the host, re-copy it into `kasm/ime/` before
> rebuilding — there is no automatic sync.

### 3. `desktop_ready` instead of a hand-rolled wait

`/usr/bin/desktop_ready` is Kasm's own gate (it blocks on `pidof $START_DE`).
Using it tracks whatever DE the base image ships, where
`until pgrep -x xfce4-session` silently stops working if that ever changes.

---

## SAM Models

`SamSegmenter::modelDir()` is hardcoded to `$HOME/.config/scribus/sam/` with no
system-wide fallback (`scribus/scimagesam.cpp:26`). Baking 43 MB of `.onnx` into
the default profile would make Kasm `cp -rp` it into **every user's home** on
first launch.

So the models are kept as one system-wide copy in **`/opt/scribus/sam/`**, and
`kasm-launch.sh` symlinks `~/.config/scribus/sam` → there at session start.

- **Option A — Bundle in image**
  Build with `--build-arg INCLUDE_SAM_MODELS=true`. Downloaded at build time
  into `/opt/scribus/sam`.

- **Option B — Bind-mount** *(recommended for production)*
  Keep the image small and mount the models read-only over `/opt/scribus/sam`.
  No profile involvement, one copy per agent:
  ```json
  "/srv/scribus-sam": "/opt/scribus/sam"
  ```

- **Option C — Leave them out**
  Smart Select degrades gracefully with a status-bar message.

> Models used: MobileSAM encoder + SAM decoder from the public
> `vietanhdev/segment-anything-onnx-models` release.

---

## Local Testing

```bash
./kasm/test-local.sh                 # build (no models) + run
./kasm/test-local.sh --with-models   # build with SAM models + run
./kasm/test-local.sh --build-only    # build only
```

Then open **https://localhost:6901** (self-signed cert — accept the warning),
log in with `kasm_user` / `password`.

---

## Files in this directory

| File | Purpose |
|------|---------|
| `Dockerfile.kasm` | The image definition |
| `kasm-launch.sh` | Installed as `$STARTUPDIR/custom_startup.sh`; waits for the desktop, links SAM models, starts fcitx5 + Scribus |
| `scribus.desktop` | Desktop/menu/autostart entry |
| `ime/*.mim` | The custom m17n tables (not available from any Debian package) |
| `kasm-workspace-config.json` | Reference config for Kasm Admin import |
| `test-local.sh` | Local build + run helper |

---

## Auto-launch mechanism

Two redundant, idempotent triggers (a `pgrep` guard prevents duplicate windows):

1. **Kasm custom startup** — `kasm-launch.sh` is installed as
   `$STARTUPDIR/custom_startup.sh`, which Kasm runs during VNC startup.
2. **XFCE autostart** — `~/.config/autostart/scribus.desktop` in the default
   profile, which fires once the session is up.

The guard matches `scribus.bin`, not `scribus`: `/usr/local/bin/scribus` is a
wrapper that `exec`s `scribus.bin`, so the running process never has the
wrapper's name.

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| **Scribus doesn't launch** | `docker exec … ls -l /dockerstartup/custom_startup.sh` and `~/.config/autostart/`. Confirm the DE is up: `pgrep -x xfce4-session`. |
| **Customisations missing with persistent profiles** | Confirm they were baked into `/home/kasm-default-profile`, not `/home/kasm-user`. `$HOME/.bashrc` existing suppresses the profile copy entirely. |
| **Malayalam keyboard is the wrong layout** | `ls -l /usr/share/m17n/ml-deshabhimani.mim`. If absent the build didn't pick up `kasm/ime/`. Check the active IM with `fcitx5-remote -n`. |
| **SAM Smart Select does nothing** | Status bar shows why. Check `ls -lL ~/.config/scribus/sam/` resolves into `/opt/scribus/sam`, and `ldconfig -p \| grep onnxruntime`. |
| **Malayalam renders as boxes** | Fonts missing. `fc-list :lang=ml \| wc -l` should be non-zero; `prefsmanager.cpp` probes U+0D15 to pick a face that actually renders. |
| **Slow / laggy UI** | Increase `shm_size` (≥ 512m) and container CPU/memory. |

---

## Notes

- `KASM_BASE_TAG` defaults to `1.19.0-rolling-weekly` — a rolling tag, per
  Kasm's guidance to avoid `latest` while still receiving security updates.
  Override with `--build-arg KASM_BASE_TAG=1.19.0` to pin.
- Single-stage build: the `-dev` packages stay in the final image. Since the
  Kasm base is already 4.56 GB, a build/runtime split saves roughly 1.5–2 GB
  and is worth doing once the image is proven, but it risks omitting a
  `dlopen`-ed runtime library (Qt platform plugins, CUPS backends) that `ldd`
  does not reveal.
- `COPY . /tmp/scribus-source` leaves the ~320 MB source tree in its own layer
  even though the next `RUN` deletes it. Replacing it with a BuildKit bind
  mount (`RUN --mount=type=bind,source=.,target=/src` plus an out-of-source
  `cmake -S /src -B /tmp/build-kasm`) removes that layer entirely. Left as-is
  deliberately: the `COPY` form is what was actually verified, and this is the
  kind of change that should be made against a green build, not alongside one.
- `hyphen`, `graphicsmagick` and `librevenge` are deliberately **not** installed:
  CMake did not find them on the development host either, so including them
  would produce a *different* binary from the tested one.
