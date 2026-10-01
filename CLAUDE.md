# CLAUDE.md — standing facts for this repo

Faircode/Suneer fork of **Scribus 1.7.3** (Qt6), used in production for
**Malayalam newspaper page layout** (Deshabhimani). Developer: Suneer A.

`master` is the upstream import. All custom work lives on feature branches —
see **NOTES.md** for the branch map, the feature inventory, and the traps.

---

## 1. The documents are Malayalam, typed through an input method

Every production document is Malayalam entered through **fcitx5 + m17n**.
The user's configured engines (`/home/s1/.config/fcitx5/profile`):

- `m17n_ml_deshabhimani` — **the default**
- `m17n_ml_mozhi`, `m17n_te_praja-sr`, `keyboard-us`

Environment: `QT_IM_MODULE=fcitx5`, `GTK_IM_MODULE=fcitx5`, `XMODIFIERS=@im=fcitx5`.

**Typing ASCII does not test a text-path change.** Latin-1 hides exactly the
things that break here: multi-codepoint clusters, reordering vowel signs,
preedit composition, and cluster boundaries that are not character boundaries.
A change that "works" on `hello world` tells us nothing.

Keep these three apart, and say which one you actually exercised:

1. **IM path** — keystroke → fcitx → preedit → commit → `StoryText`.
   Only exercised with fcitx running *and* a Malayalam engine active in the app.
   Code: `canvasmode_edit.cpp::inputMethodEvent`, `canvas.cpp::inputMethodQuery`,
   `scribusview.cpp` forwarding, char-style inheritance at the caret.
2. **Layout/render path** — Malayalam already in the story. Reachable by SLA
   surgery or the Python scripter. This does **not** prove the IM path works.
3. **ASCII path** — proves nothing about either. Never report it as a text test.

Evidence for a text-path claim must show real Malayalam: codepoints in
U+0D00–U+0D7F in the story or the saved SLA, or a screenshot with the glyphs.

**Do not inject synthetic input into DISPLAY :0.** That is the user's live
Xfce/Kasm desktop and they are watching it; `xdotool key`/`click` there hijacks
their keyboard. Read-only screenshots (`DISPLAY=:0 import -window <id>`) are
fine. Drive automation on an isolated `Xvfb :77`/`:99` instead, or write out the
exact steps and let the user perform them.

## 2. Print changes are not done until the user confirms on the real printer

The real printer is **`HP_M706n`** (`ipp://<OFFICE_NETWORK_PRINTER>/ipp/print`,
driverless `everywhere`, A3 default, the system default queue). The actual
address is in `CLAUDE.local.md`, which is gitignored — this repo is public, and
internal network addresses do not belong in it. Use the queue name `HP_M706n`
in commands; CUPS resolves it without needing the address.

A captured `.ps`, a print-to-file, or a Ghostscript render is **generation-side
evidence only**. Report it as such and then ask the user to confirm on paper.
Two measured reasons this is not pedantry:

- `-o fit-to-page=true` is a CUPS **filter** option. A PostScript queue can
  receive the job unfiltered, so the page stayed at document coordinates and the
  printer clipped it — the file was perfect, the paper was one quadrant. Page
  fitting is now emitted as `translate`/`scale` in the PostScript itself
  (`6c28fcf`); never delegate it to CUPS again.
- `ScPrintEngine_PS` runs `system("lpr …")` and **ignores the exit status**, so a
  failed print is completely silent and `doPrint()` still returns true. Never
  judge "did it print" by the absence of an error or by a window appearing.

Safe local harness: put a stub `lpr` earlier in `PATH`
(`~/scribus-crashlogs/tabletest/fakebin/lpr`) that copies its last file argument.
F9 then produces a real, greppable print job with no dialog and nothing reaches a
queue.

## 3. State what you ran and what you read

For every claimed pass, give the **exact command** and the **specific output you
checked** — the line, the value, the file size, the codepoints. "Built and
tested, works" is not a result.

Label every claim as one of: **verified by me** (with the evidence),
**verified by the user**, or **not exercised**. Carry "not exercised" forward
into later summaries instead of letting it quietly become "done" — e.g. the
proof paper-unknown fallback branch has still never run.

If something failed, say so with the output. If a step was skipped, say that.

---

## Build and install

```bash
cmake --build build -j$(nproc)     # FULL build — not --target scribus
cmake --install build              # required
```

RelWithDebInfo, `CMAKE_INSTALL_PREFIX=/usr/local`.

Scribus loads format/import/export plugins from
`/usr/local/lib/scribus/plugins/` — the **installed** ones, even when you run
`build/scribus/scribus`. Change the layout of `CharStyle`, `ParagraphStyle`,
`ScribusDoc` or `PageItem` and reinstall only the main binary and you get an ABI
mismatch: garbage pointers, SIGSEGV on document load. Check that the plugin `.so`
timestamps match the new binary.

`/usr/local/bin/scribus` is a wrapper doing `exec -a scribus … scribus.bin` so
prefs stay in `~/.config/scribus/` rather than `~/.config/scribus.bin/`.

Asserts are **live in release builds**: `scribus/text/index.h` does
`#undef NDEBUG` for the whole text subsystem, so a bad text position aborts the
process rather than degrading.

## Environment

- The desktop user is **s1** (`/home/s1`); the agent shell runs as **root**.
  Test documents the app must read have to live under `/home/s1` — s1 cannot
  read the root-owned scratchpad. `chown s1:s1` any file you create in the repo.
- Headless: `xvfb-run -a scribus -g -ns -py script.py`, `-pr <dir>` for isolated
  prefs. Pass the `.sla` as argv rather than calling `openDoc()` from a startup
  script.
- GUI automation on Xvfb works via `xdotool windowfocus --sync <id>` (XSetInputFocus,
  no WM needed) plus XTEST keys/clicks. `xdotool key --window` uses XSendEvent,
  which Qt6 silently ignores. **Shortcuts bound to a `ScrAction` never fire** in
  that harness — drive features through their menu entry, and hand shortcut
  verification to the user.

## Conventions

- Commit subjects: `suneer: <what changed, in plain language>`.
  Co-author trailer: `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- User-facing feature notes go in `SUNEER_CHANGES.md`; the engineering index
  (feature → branch → trap) is `NOTES.md`.
- The working tree carries many untracked `.bak*`/`.orig`/`.broken` scratch files
  from earlier sessions. Leave them alone unless asked.
- Discuss before patching when a fix has design consequences for undo, file
  format, or the layout pipeline — several traps in NOTES.md came from a fix
  applied at the wrong layer.

## Reports and change log (always)

- Save every investigation/diagnosis result as a file in
  `/home/s1/Desktop/claude/` named `result-YYYYMMDD-HHMM-<short-topic>.md`.
  Include: the problem as the user described it, what was checked, the cause,
  measurements, and what is still unknown.
- Save every code change as `/home/s1/Desktop/claude/change-YYYYMMDD-HHMM-<short-topic>.md`,
  with: what changed and why, the files touched, the commit hash if committed,
  how to test, and how to undo. Save the diff next to it as
  `change-YYYYMMDD-HHMM-<short-topic>.patch`.
- Write the explanations in simple Malayalam; keep file names, code and
  technical terms in English.
- Create the folder if it doesn't exist (`chown s1:s1` what you create).
  Never overwrite an older file; always make a new one.
- At the end of each task, tell the user the file names you saved.
