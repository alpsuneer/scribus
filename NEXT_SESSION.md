# NEXT_SESSION.md

Handoff for the Scribus Image Editor work, Aug 2026, branch `feature/ctp-output`.
Engineering index lives in NOTES.md; this file is just "where we left off".

## Shipped

- Branch now **builds from a clean checkout**: earlier commits had added CMake entries, headers and call sites without `git add`ing the implementation files. Verified by building a pristine worktree (0 errors, 0 undefined refs).
- Registered `tool-move` and `select-brush` in the iconset manifest; both were loading Qt fallback icons and logging IconManager warnings.
- Per-tool cursors: 12 editor tools, 12 distinct cursors (5 new `cursor-select-*.svg`, rest reused).
- Cursor infrastructure: `updateCursor()` is now the single authority and targets the **viewport**; cursor primed in the view constructor.
- Pen Path tool (`P`): cubic beziers, click=corner / drag=smooth, Alt=convert, Backspace, Enter or double-click commits.
- Pen overlay crash fix: a `boundingRect()` that varied with zoom corrupted the scene index (SIGSEGV in `QGraphicsView::paintEvent`).
- Editor undo stack capped at 30 commands.
- Editor loads images at **full resolution** via `ScImage::loadPicture` — TIFF/PSD/JXL used to open as the 96x72 preview.
- Vector sources (PDF/EPS/PS/AI) excluded from the editor; CMYK sources warn before RGB conversion.
- **Data-loss fix**: `writeImageToFile` now refuses unwritable formats instead of letting `QImageWriter` truncate the file to 0 bytes.
- Crop / masked-flatten / erase write a **PNG sidecar** when the source format can't be written; source left byte-identical (sha256-verified).
- Erase-to-transparent on Delete/Backspace.
- JPEG alpha-composite no longer drops DPI (was silently rewriting 300dpi as 96dpi).
- Lasso: WindingFill (self-crossing traces lost wedges), screen-space thresholds, two-pass visible preview.
- RectMarquee / EllipseMarquee / SAM: screen-space thresholds via `imageDistance()`.

## Open items

- **Scripter `QImage::save()` sites bypass the `writeImageToFile` guard** and can still truncate a user file when handed an unsupported format:
  `plugins/scripter/api_imageexport.cpp:142`, `plugins/scriptplugin/objimageexport.cpp:168` and `:202`,
  `plugins/scriptplugin/cmdmisc.cpp:138`, `plugins/export/pixmapexport/export.cpp:226` (lowest risk — format comes from a combo).
- **`_rembg` document undo is broken**: `ui/suneercontrolbar.cpp:3042` sets `item->Pfile = outputPath` *before* `loadPict`, so `pageitem.cpp:10201` records the sidecar as `OLD_IMAGE_PATH` and Ctrl+Z can't restore the original. Same pattern at `:3645`. The erase/crop/flatten paths deliberately do **not** pre-assign.
- **PolygonLassoTool preview is invisible on light images**: white-only pen at `ui/tools/polygonlassotool.cpp:125`. Should adopt LassoTool's black+white two-pass (measured contrast span 17/255 vs 255/255).
- **`resources/iconsets/CMakeLists.txt` uses `file(GLOB)`** (`:115`, `:127`), evaluated at *configure* time — new artwork needs `cmake -S . -B build` before `cmake --install`, or it silently doesn't deploy.
- **`icons/1_7_0/1_7_0.xml` is a stale duplicate.** IconManager reads the root copy, `icons/1_7_0.xml`.
- **Five committed 16px SVGs are unregistered**: `select-{lasso,ellipse,polygon,rectangular,smart}.svg`. Harmless today — the editor loads those tools through `makePngTool`, which uses the `-24.png`/`-48.png` files, not the manifest. They only need ids if the toolbar ever moves to the SVGs.
- **Anything added to the build must be committed with its sources.** The clean-checkout breakage came from committing `CMakeLists.txt` entries, headers and call sites while the `.cpp` files stayed untracked; local builds hide this completely. `git status` before committing a feature.
- **GUI automation**: the "Qt menus intermittently refuse to open" symptom was self-inflicted — the editor window is not at (0,0) (seen at X=1,Y=25). Always `eval $(xdotool getwindowgeometry --shell $W)` and offset. Screenshots don't capture the pointer; use the XFixes probe (`xfixes.get_cursor_image(display, root)`) to verify cursors.
- **`pgrep -f` / `pkill -f` match the invoking shell's own command line.** `pkill -f "Xvfb :77"` kills the shell running it (exit 144), and an `until ! pgrep -f "cmake --build"` watcher never exits because it finds itself. Collect PIDs first and `kill` those, or use a self-excluding pattern like `ps -eo args | grep '[u]ntil'`. Cost three self-killed shells and a bogus "8 builds still running" reading in this run.

## Design decisions worth remembering

- **Sidecar PNG** for formats Qt can't write (erase always; crop/flatten only when `canWriteImageFile()` is false) — `derivedImagePath(Pfile, tag, "png")`, then repoint via `doc->loadPict(path, frame, false, true)` **without** pre-assigning `Pfile`.
- **CMYK** warns on open with Cancel / "Continue in RGB" — deliberately no "don't show again"; the decision is forced every time.
- **Vector formats excluded entirely** from the editor rather than rasterised silently.
- **Undo limit 30** counts *commands*, and a selection-driven edit pushes two (selection change + base change), so it is roughly 15 visible edits.
- **All screen-space thresholds go through `ImageTool::imageDistance()`** — never a bare image-pixel constant, or the behaviour drifts with zoom.
- **Pen anchors are append-only in v1**: Backspace to undo, no drag editing; refine after commit with Add/Subtract.
- **Cursor hotspots are per-glyph.** Shared-crosshair badge designs use **(15,15)** (crosshair centre in the 32px art). Borrowed art differs and must be checked: `cursor-move` (1,1), `cursor-color-picker` (2,29), `cursor-zoom-in` (7,7) — that one is a 16x16 SVG living in the `32/` folder.
- **`boundingRect()` must never depend on the view transform.** Pad with a constant upper bound; use the live scale only inside `paint()`.
