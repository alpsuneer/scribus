# NOTES.md — custom features, branches, and traps

Engineering index for the Faircode/Suneer Scribus 1.7.3 fork.
User-facing descriptions live in `SUNEER_CHANGES.md`; working rules live in
`CLAUDE.md`. This file answers "what did we build, on which branch, and what bit
us while building it".

---

## Branch map

```
master (7afb2db)                       upstream 1.7.3 import + 1 commit
└─ background-removal (83b3447)        101 commits — the bulk of the fork, through 2026-08-02
   ├─ legacy-overflow-prompt   (d5a69ff)     single-purpose cherry-pick sources:
   ├─ fix/caret-style-inheritance (f6b0c5a)  each is background-removal + 1–2 commits,
   ├─ fix/cellstyle-border-load (4bc0da8)    kept clean so the fix can be merged into
   ├─ fix/table-insert-undo    (b009c94)     any other feature branch
   ├─ fix/undo-bullet-marks    (438c3a8)
   ├─ bullet-image-fallback    (6cd476c)     image-bullet robustness + merge of fix/undo-bullet-marks
   └─ autofit-typography (d01f71e)     +17 commits: Auto Fit Text, image bullets, crash
      │                                 capture, undo/table/cellstyle fixes, Proof preset
      └─ print-proof (c895c37)         +7 commits: proof printing — CURRENT BRANCH
```

**`print-proof` is the superset** — every feature below is on it. The `fix/*`
branches hold the same changes under different hashes (cherry-picked), so do not
merge them into `print-proof` expecting new content.

Everything in the fork sits on top of two `master` commits; there is no upstream
remote tracking, so a rebase onto a newer Scribus is a manual job.

---

## Feature inventory

Branch column = where the work was introduced. Unmarked features came from
`background-removal`.

### Text and typography

| Feature | Key commits | Branch |
|---|---|---|
| Auto Fit Text — scales typography (font size/glyph scale/tracking/word space), never the frame; baseline saved in the file | `509359b`, `5e898d8` | autofit-typography |
| Fix Overflowing Frames — grow-only batch autofit | `8602008` | |
| Legacy-overflow prompt when an older file is opened | `d5a69ff` | legacy-overflow-prompt |
| Nested styles (InDesign-style, layout-time char overlay) | `4e80b33` | |
| Paragraph rules — rule above / rule below, 28 style attrs, one render hook shared by canvas/PDF/PS/XPS/SVG | `e471a0f` | |
| Paragraph shading — per-paragraph background band, 10 attrs | `a4fadee` | |
| Image bullets — PNG + SVG on bulleted list styles, base64-embedded in the SLA, size modes, x-height anchor | `971ddb5`, `af08ca1`, `8770ab3` | |
| Image bullet robustness — never fail silently, downscale huge sources, draw with Show Images off | `072c3e8`, `d272bff`, `87bbd75` | bullet-image-fallback → autofit-typography |
| Styled copy/paste (Ctrl+Shift+C / Ctrl+Shift+V) | `8494cbf` | |
| Autoflow to New Pages | `9f09088` | |
| Typed text inherits the caret's character style at a paragraph end — **DONE, verified by the operator through the IM path** (see below) | `c895c37` | fix/caret-style-inheritance |
| Malayalam-capable default text font — `initDefaults` prefers one, plus a one-time repair of an existing profile | `5f6be08` | print-proof |
| Space Above survives a spanning headline | `456ea5d` | |
| Outward text outline — `outlineOutward` CharStyle attribute, canvas matched to export painters | `882774a`, `68cef64` | |
| Convert Text Frame to Table; Ctrl+A selects all cells | `1343c34`, `71fc484`, `1fe5183` | |
| Auto-hyphenation defaults; auto-fit/auto-hyphenate on open added, then removed | `fa3e4c5`, `1b97ec6`, `a6ef17a`, `1863b8f` | |

### Layout

| Feature | Key commits | Branch |
|---|---|---|
| **Auto Arrange Frames** — **DISABLED, pending rewrite.** `autoarrangeengine.{h,cpp}` + dialog | `c58f023` ⚠, disabled in `df03538` | |
| Text-frame edge-resize band; overflow icon off the corner | `e6b0cf0` | |
| Overflow-click on an empty page creates a source-styled linked frame | `e03d7f6` | |
| Double-click drills into groups (text frames straight to edit) | `698507b` | |
| Faircode Frames shipped scrapbook — 23 templates in 4 waves | `c117e41`, `83e7e0b`, `957c424`, `181081d`, `d45958d` | |

⚠ Auto Arrange landed inside a commit titled *"add the image editor sources under
version control"* (2026-07-28, a sweep of several untracked files), so
`git log -- scribus/autoarrangeengine.cpp` returns one commit whose message says
nothing about auto-arrange. Search by symbol, not by commit message.

⚠ **Auto Arrange destroyed a production broadsheet and is now unreachable from
the UI.** The engine does not compact frames in place — it *re-lays out* the
page, discarding every position and re-stacking each column from `area.top()`.
Three structural faults, all in `autoarrangeengine.cpp`:

1. **Overlap.** `analyzeFrame()` computes a `columnSpan`, but the stacking loop
   buckets frames by `startColumn` only. A frame spanning four columns sits in
   column 0's stack while columns 1–3 stack their own frames from the top of the
   page straight through it. Any multi-column ad or headline gets overlapped.
   `columnSpan` is used to set width and nothing else.
2. **Empty bottom.** Only the *last flexible* frame is stretched to close a
   column, so a column of nothing but images/groups/headlines (`lastFlexIdx ==
   -1`) ends early. And `avail = area.height() - fixedTotal` can go negative,
   clamping `scale` to 0.1 and collapsing every text frame in the column.
3. **Columns silently deleted.** `minColWidth = maxW * 0.5` discards any span
   under half the widest as a "gutter"; a page with one wide ad block loses real
   narrow columns, and their frames fall to the nearest-left-edge fallback and
   pile into a neighbour.

Also: z-order and linked text chains are never consulted (`grep nextInChain`
returns nothing on the engine, yet it resizes chained frames — there is a live
`"TEXT LENGTH CHANGED … possible content loss!"` warning), and
`ScribusDoc::OnPage()` uses `intersects`, so a pasteboard item merely touching
the page edge is claimed and dragged into a column.

Undo is *not* the problem: `arrange()` wraps the whole run in one
`UndoTransaction` and the geometry setters record through
`checkChanges()`/`moveUndoAction()`, so one Ctrl+Z reverts the geometry. Text
redistributed across a linked chain may not come back, though.

Disabled by unbinding the default shortcut (it was **Ctrl+Shift+F**, one shift
key from Ctrl+F Search/Replace) and removing the Item-menu and context-menu
entries. The action and its Preferences → Keyboard Shortcuts entry survive, so
it can still be bound deliberately. **A shortcut saved in an existing
`scribus172.rc` overrides the compiled default** — clear it in Preferences or
the old binding stays live.

The intended replacement is *compact and align to the grid*: keep each story in
its column region and reading order, move each story's text+image as one block,
snap to column guides, close vertical gaps, leave the ad alone, preserve spans.
Dry-run-first, per the operator.

### UI and panels

| Feature | Key commits | Branch |
|---|---|---|
| SuneerControlBar — text/image/table control toolbar; live font preview, gap spins, border and padding controls | `17c7747`, `7520912`, `5270cfd` | |
| Proof Print button on the control bar | `a537c5a` | print-proof |
| Paragraph Styles panel — News Browser as a third tab, in-app help, dark-theme readability, Next Style chain icon | `a0dbf1b`, `39d7126`, `a3090b2`, `da7ef2a`, `f757648`, `3236fe11`, `c4c2bf4` | |
| Default workspace layout, Faircode splash, title-bar build stamp | `3590de5`, `f57058a`, `09c2b02` | |
| Paragraph Shading popup on the control bar — local override, never edits the style; embeds the Style Manager's `SMPShadeWidget`; `itemSelection_ResetParagraphShading()` ⚠ | `7a02607` | feature/paragraph-shading-popup |
| Stock toolbars start hidden on a new profile | `e8a34ae` | autofit-typography |
| Text Distances section hidden from content properties | `505a048` | |

⚠ The Style Manager's `SM*` widgets (`SMCheckBox`, `SMSpinBox`, `SMColorCombo`,
`SMScrSpinBox`, `SMScComboBox`) expose `useParentValue()` as a **one-shot
consuming read** — it returns the flag and immediately clears it. That suits the
Style Manager, which reads each widget once per `showShade()` cycle, but any
live-preview UI reading on every change gets `false` from the second read on and
writes every attribute as a local override, silently killing later inheritance
from the style. The shading popup therefore snapshots the paragraph's local
style and its parent when it opens and compares against that instead. Reuse an
`SM*` widget outside the Style Manager and you must do the same.

⚠ `itemSelection_ApplyParagraphStyle()` merges only attributes that are *set*, so
it can add or change an override but can **never remove one**;
`itemSelection_EraseParagraphStyle()` removes *all* direct paragraph formatting
(indents, spacing, alignment) and is far too broad for a targeted reset. Hence
the narrow `ScribusDoc::itemSelection_ResetParagraphShading()`. Note also that
`ParagraphStyle::eraseStyle()` is value-matched, not attribute-matched.

### Image editor (ScImageEditor)

| Feature | Key commits | Branch |
|---|---|---|
| Sources under version control + build wiring | `c58f023`, `f7012a5` | |
| Effect stack, filter dialogs, undo, tools, options bar, async SAM encode, cached previews | `6e0ce60`, `b92dde5`, `ae4def5`, `fc3ba7d` | |
| Resize Image (resample to what the frame needs), visible self-exiting crop mode, image write error reporting | `b082928`, `9f77571`, `6c93954` | |
| Embed in SLA (Base64) actually embeds | `7f610b8` | |

### Printing and export

| Feature | Key commits | Branch |
|---|---|---|
| Proof Print preset — one-click grayscale draft, 150 dpi | `cb0a160` | autofit-typography |
| Raster resampling to the proof resolution (the lever that actually worked) | `f8a36b5` | autofit-typography |
| F9 binding | `d01f71e` | autofit-typography |
| Proof adapts to the proof printer's paper; page scaling emitted in the PostScript; slug line; system-default printer resolution; status-bar report | `7fe169d`, `6c28fcf`, `4e1b948`, `0083dcb`, `1e29bbc` | print-proof |
| Newspaper PDF export defaults + News_Paper preset (X-1a, IFRA26S) | `0a5571a`, `11e552b` | |

Measured effect on an 8-photo page, print-to-file: **490 MB → 5 MB** (≈98×);
grayscale alone gives 124 MB, resampling alone 20 MB. Correctness checked by
rendering both `.ps` at `gs -r18`: identical page dimensions, content bbox
identical to the pixel, mean pixel difference 1.3%.

### File format and compatibility

| Feature | Key commits | Branch |
|---|---|---|
| Load Span Columns / Next Style from 1.5.x documents | `9ab4870` | |
| Accept the legacy all-caps `LINESPMODE` spelling | `14c1cab` | |
| Text-wrap gap offsets serialized in all three SLA formats | `4895488` | |
| Per-side frame borders survive copy/paste and save | `f9026b9` | |
| Cell style borders survive loading | `0eb65ec` | fix/cellstyle-border-load |

### Undo correctness (a recurring theme — most of these were one-line-looking bugs)

`e798d99` + `a192bef` bullet marks · `6aea711` insert table · `eb27733`
applyColumnConfig · `83b3447` image scale · `c6ff197` style chain · `08a40fb`
multi-frame autofit · `2bcd5be` wrap offsets · `1d425e8` size/line-spacing
steppers · `4f1eb75` per-side border flags.

### Shortcuts and infrastructure

Malayalam DTP + Photoshop keysets and conflict detection (`2bf9c59`, `b924864`);
crash capture via `SCRIBUS_NO_CRASH_HANDLER` (`eb27b87`); launcher wrapper
`exec -a` + DESTDIR-aware install (`c6e9be0`, `855fb3a`).

---

## Key traps

### 1. The SLA 1/10 grid

Font size, glyph scaling and tracking are whole units of **1/10 pt and 1/10 %**
on disk — every loader does `qRound(value * 10)`. Anything that captures a
character-style value in memory, writes it, and expects to read the same value
back must snap to that grid first, or a save/reload silently drifts the value.

This is why Auto Fit Text snaps its baseline before recording it:
`pageitem_textframe.cpp:6456-6464` (`qRound(cur.fontSize() / fs)` and friends),
declared at `pageitem_textframe.h:203`. The related rule inside the fit search:
divide the recorded factors out **once**, then compute every trial from those
numbers — never from the previous trial's already-rounded output, or seventy
iterations walk the baseline away from where it started.

Same units elsewhere: `SuneerControlBar` font size (`suneercontrolbar.cpp:2129`,
25 pt ⇒ 250), CharStyle outline width in tenths of a percent
(`suneercontrolbar.cpp:2197`), and the headline detection in the auto-arrange
engine (`autoarrangeengine.cpp:308`, `fontSize() > 200.0` means 20 pt).

### 2. Hidden-item visibility

`PageItem`'s constructor seeds `imageVisible` from the document's
`guidesPrefs().showPic` — the **View > Show Images** toggle. An item created
while that toggle is off is *born invisible*, and the toggle can never revive it,
because the toggle only walks `DocItems` and `MasterItems`
(`scribus.cpp:6466-6495`) and internal items are in neither.

That is exactly what happened to image bullets: blank on canvas in any document
saved with Show Images off, while PDF export drew them correctly (pdflib never
consults `imageVisible`). Fix: create decoration items **explicitly visible** —
`item->setImageVisible(true)` at `scribusdoc.cpp:575`, commit `87bbd75`. A bullet
is style decoration like a glyph, not a placed photo the preview toggle manages.

Generalise: any hidden/internal `PageItem` you create inherits document display
state at construction and is unreachable by the toggles that would fix it later.

### 3. The pasteboard blind spot

Items on the pasteboard have **`OwnPage == -1`**. Any loop that iterates pages,
or filters `OwnPage == pageNr`, silently skips them — they are invisible to the
feature while being perfectly visible to the user.

- Scan the whole document with `doc->DocItems`, which includes the pasteboard;
  it is also unambiguous right after load, unlike `*doc->Items`
  (`scribus.cpp:9465-9468`).
- When comparing items *by page*, remember that `OwnPage == -1` makes all
  pasteboard items compare equal to each other (`scribus.cpp:9440-9443`).
- The auto-arrange engine deliberately gathers only `OwnPage == pageNr`, so
  pasteboard frames are left alone — that one is intentional, not a bug.

Stock code that already knows this: `tocgenerator.cpp`, `documentchecker.cpp`
(orphan check), `outlinepalette.cpp`, `picstatus.cpp`.

### 4. Undo and mark removal

The first captured crash: `SIGABRT` on `assert(pos <= length())` in
`StoryText::eraseStyle` while undoing an apply-paragraph-style transaction
(pos 1241, length 1234). All stock code, triggered by the bullet/news workflow.

Chain: `itemSelection_ApplyParagraphStyle` records absolute story positions
including a trailing `POS = length`. The **next layout pass** then deletes the
`BulNumMark` character of every paragraph whose new style has no bullet — with
**no undo state** — so the story shrinks by one char per formerly-bulleted
paragraph. Ctrl+Z replays the recorded positions against the shortened story and
aborts. (Asserts are live even in release builds: `scribus/text/index.h` does
`#undef NDEBUG`, so this aborts the process rather than misbehaving.)

Two-layer fix, `e798d99` + `a192bef`:

- **Safety net** — `applyStyle`/`eraseStyle` clamp a stale position and
  `qWarning` instead of aborting (`text/storytext.cpp:1774`, `:1821`).
- **Root cause** — mark removal is recorded inside the style transaction:
  `ScribusDoc::removeOrphanedBulNumMarks()` (`scribusdoc.cpp:9822`), called from
  `itemSelection_{Apply,Set,Erase}ParagraphStyle`, pushed **after** the style
  states so reverse replay re-inserts the marks before any position is needed.

**Design note worth keeping:** recording the removal at the layout site
(`pageitem_textframe.cpp`) would neutralise undo — undo re-inserts the mark, the
next paint removes and re-records it, clearing redo. Undo and layout would fight
each other forever. Record in the command, not in the layout. Layout keeps an
unrecorded fallback plus a `qWarning`.

Regression battery: `~/scribus-crashlogs/repro/run_undo_bullet_test.sh [char|image]`.

### Other traps that cost real time

- **Plugin ABI** — plugins load from `/usr/local/lib/scribus/plugins/` even when
  running the build-dir binary. Change a shared struct, rebuild only the main
  binary, and you get SIGSEGV on document load. Full build **and**
  `cmake --install`.
- **Newspaper guides come in pairs** bracketing narrow gutters, so "every gap is
  a column" produced 15 fake columns. The engine drops spans narrower than
  0.5× the widest.
- **Static SLA parsing lies about positions** — grouped items store
  pre-transform coordinates (a footer group at x≈1045 with children stored at
  x≈2045). Get real positions from a running binary, not from the XML.
- **1.7.x SLA names differ from 1.5.6** — `SpanColumns`/`NextStyle` vs
  `FullSpan`/`nxtStyle`, `<ParagraphStyle>` vs `<STYLE>`,
  `<PageObject>`/`ItemType` vs `PAGEOBJECT`/`PTYPE`. Legacy `1` maps to `-1`.
- **Ctrl+C/V does not use the copy constructor** — it serializes items to SLA
  XML, so a new `PageItem` field must also be written in `SetItemProps` in all
  three save plugins or it vanishes on paste.
- **Three parallel shortcut dispatchers** exist; only one is visible to the
  Preferences duplicate check. Shortcuts that must fire while editing text need
  `Qt::ApplicationShortcut`, set in the same init function that creates the
  action.
- **`getBool` writes missing defaults** on first read, so changing a default
  later has no effect on existing profiles — gate such changes explicitly.
- **`-o fit-to-page=true` is a CUPS filter option** — a PostScript queue may
  never see it. Emit the scaling in the PostScript. See `CLAUDE.md` §2.
- **Scribus has no per-glyph font fallback.** A missing glyph is a `.notdef`
  box; `getSubstitutions()` only swaps whole *missing* fonts, and the "fallback"
  shaper is HarfBuzz's box renderer. So the application default font must itself
  cover Malayalam — decide that with `canRender(U+0D15)`, never by font name.
  `fc-list :lang=ml` is not the authority either: it omits DZ DB Text, which has
  81 Malayalam codepoints including KA.
- **The prefs XML is `scribus172.rc`, not `prefs172.xml`.** `prefs172.xml` is the
  `PrefsFile` *context* store (`<context name="…">`); `ApplicationPrefs` — the
  `<ItemTools FontFace="…">` element and the shortcut table — lives in the `.rc`.
  Grepping the wrong one returns nothing and looks like the setting is absent.
- **The Malayalam-DTP first-run dialog is gated by `QSettings`, i.e. by `$HOME`,
  not by the `-pr` prefs dir.** A test with an isolated prefs dir but the real
  HOME shows no dialog; a fresh HOME shows it and blocks startup.
- **The scripter's `print()` does not reach the terminal.** Headless probes must
  write a file (or save a document) to report anything.
- **`pgrep -f scribus.bin` never matches a running Scribus.** The launcher does
  `exec -a scribus …/scribus.bin`, so argv[0] is `scribus`. Checking liveness by
  that pattern reports "crashed" for a perfectly healthy app — match `scribus`
  (and remember `pkill -f "[X]vfb :99"` still kills your own shell if the rest of
  the command line contains that string).

---

## Regression batteries

25 scripts in `/home/s1/scribus-crashlogs/{repro,tabletest}/`. Run them **one at a
time** and read the script before trusting a red result — several carry defects
that produce failures having nothing to do with the code under test.

**Harness defects found 2026-08-05** (all pre-existing, none caused by the code):

- **`key --window` is XSendEvent, which Qt6 Scribus silently ignores.**
  `run_undo_bullet_test.sh` sent undo/redo/undo/save this way, so every key was a
  no-op and it failed at the final assertion, looking exactly like an undo
  regression. With XTEST (`xdotool windowfocus --sync W; xdotool key K`) both
  variants pass. Fixed copy: `repro/run_undo_bullet_test_safe.sh`. Ten scripts
  still use the broken form — grep `key --window` before believing any of them.
- **`pkill -x scribus` matches nothing** (comm is `scribus.bin`, see the trap
  above), so every script leaks its instance — *except* against
  `scribus-debug`, whose binary really is named `scribus`. So a parallel script's
  cleanup **kills the debug-run battery mid-test**: that is what produced a
  SIGTERM core and a "crashed or died on save" failure here. Never run these
  concurrently.
- **`f9_final_test.sh` does not parse** — `bash -n` reports a syntax error at
  line 67, a dangling `continue; fi` left inside a `for` loop by an earlier edit.
  Use `f9_editmode_test.sh` (diagnostic only) or `proofbtn_test.sh` (asserts via
  a stub `lpr`) instead.
- **`im_caret_test.sh` runs a bare `pkill fcitx5`**, which kills the operator's
  live input method on `:0`. Session-safe copy with scoped cleanup:
  `im_caret_test_safe.sh`.

**Caret style inheritance is DONE — verified by the operator on the real desktop
(2026-08-05):** Malayalam typed through fcitx at the end of a red heading comes
out red. That closes the one path the harness cannot reach. Headless evidence
agrees but does not substitute for it: `caret_mal_test.sh` produces
`<Content FontSize="24" FontColor="Red" Chars="AAAകകക"/>` — `U+0D15` ×3 landing
in the red 24pt run — via `xdotool type`, which delivers key events directly and
never enters `inputMethodEvent`.

**The IM path is otherwise NOT testable headlessly.** `im_caret_test.sh` is meant
to be the one battery that drives real fcitx5 + m17n Malayalam, but it cannot
reach a verdict here: fcitx5 is single-instance per D-Bus session (needs
`dbus-run-session`, else the test silently uses the operator's fcitx5 on
`keyboard-us` and types ASCII); `XDG_RUNTIME_DIR` is `/run/user/0` and unwritable
by `s1` under the agent shell; and — the real blocker — **once a Malayalam engine
is active the `e` that opens Edit Contents is consumed by the IM**, so edit mode
never opens and nothing is typed at all. Switching the engine after edit mode via
`fcitx5-remote -s` did not stick (`-n` returns empty). Until this is solved,
IM-path claims must come from the operator on the real desktop.

## Parked features — built, working, deliberately not shipped

These are complete and buildable but **kept off the build branch by choice**.
They are not dead code and not lost work; do not "clean them up", and do not
re-apply them to `print-proof` without asking.

### News auto-style (Ctrl+Shift+A) + headline size cycle (Alt+Up/Down)

**Operator decision, 2026-08-05: not wanted in the build.** Recovered from a
dangling commit earlier the same day, shipped briefly, then removed again.

*What it does.* In text-frame edit mode with a selection, Ctrl+Shift+A walks each
selected paragraph, detects a leading `"Label: "` prefix and applies the mapped
paragraph style from the production `sample.sla`, stripping the label (tolerating
leading whitespace from a first-line indent). Unlabeled paragraphs default to
Body. Missing styles abort the run with an actionable status message rather than
creating styles silently. Whole run is one `UndoTransaction`, processed
back-to-front. Label → style: Kicker=`12 Kicker`, Headline=`32 M`, Byline=`07
Byline`, Dateline=`01 Dateline`, Lead=`11 Kicker Lead`, BodyNoIndent=`03
BodyNoInd`, Body=`02 BodyText`, Highlights=`08 Blurb`. Alt+Up/Alt+Down step the
current headline paragraph through the size cycle `10 M` … `64 M`, `68 B`, `72
B`, `80 B`, one undo step per press.

*Where it lives — two refs, both gc-safe:*

| Ref | Points at | What it is |
|---|---|---|
| `feature/news-autostyle` | `6f2adbd` | conflict-resolved against the modern tree — **use this one** |
| `rescue/news-autostyle` | `1302771` | the pristine 2026-07-25 original, based at `e03d7f6` |

*To ship it again:* `git cherry-pick 6f2adbd` onto the build branch. Expect the
same four conflicts as before if the surrounding code has moved — this commit and
*Fix Overflowing Frames* both append to the Extras action creation, the `connect`
block, `setTexts`, and the Extras menu string list. Keep both sides.

*Two side effects to remember when reinstating it,* because they are the reason
the removal was not a one-line delete:

1. It **reclaims Ctrl+Shift+A from Deselect All** (`defKeys.insert("editDeselectAll",
   QKeySequence())`). Removing it gives Deselect All its shortcut back.
2. It **generalises `enforceStyledClipboardShortcuts()` into a reserved-shortcut
   table** covering Ctrl+Shift+C/V *and* the three new combos. Reverting restores
   the earlier hand-rolled version, which still protects Ctrl+Shift+C/V — verified
   after the revert at `scribus.cpp:5149`. Anyone re-applying must check that the
   styled-clipboard protection is not doubled up.

*Removed from `print-proof` by* `279297b` (a revert commit, not a history rewrite,
so `5f6be08` and every other hash on the branch stayed put).

## Lost and in-progress work

**News auto-style: recovered, then parked at the operator's request.** Commit
`1302771` was dangling (in no branch, absent from the binary); it was recovered,
cherry-picked, shipped, and then removed again the same day when the operator
decided against it. It is safe on two branches — see **Parked features** above,
which is the authoritative entry.

**Was never committed, now REWRITTEN: the Malayalam default-font migration**
(`5f6be08`, `print-proof`). The original was searched for exhaustively and existed
nowhere — no branch, no dangling commit, none of the 29 dangling blobs in
`.git/lost-found`, no `.bak` file, not in the binary — so it was rebuilt from the
design rather than recovered. See "Malayalam-capable default font" in the feature
inventory above for what shipped.

**Dangling-commit audit (2026-08-05).** `git fsck --lost-found` found nine
dangling commits; all nine now carry refs so `gc` cannot collect them
(`rescue/news-autostyle` plus tags `rescue/dangling-<sha>`). Eight are `git stash`
snapshots (two parents, autogenerated "WIP on <branch>" / "On <branch>" messages)
or a superseded duplicate — `3ee67b2` is an earlier nested-styles commit
superseded by `4e80b33`. Each was measured against `print-proof` line by line:
every one is ≥99.5% contained, and the residuals are older revisions of lines
that were later edited. The two that looked most like real work were checked by
hand and are both superseded — `0144fcd`'s `ScOldNewState<ParagraphStyle>` /
`APPLY_PARASTYLE` recording is in `ParagraphStylesPanel.cpp:1165` via `eb27733`,
and `361446a`'s `ImageEffects()` guard is at `scribus.cpp:10600` via `fc3ba7d`,
comment and all. **Only the news auto-style was genuinely lost.**

**Built, uncommitted on `print-proof`, awaiting the operator's real-printer
matrix: the Proof Print dialog.** F9 now opens it — the silent one-click path is
gone, because the paper a proof is scaled onto depends on what is actually loaded
in the tray, which only the operator knows. (Shift+F9 was never implemented and
is not needed: `grep -rn "Shift+F9"` finds nothing.)

Files: `scribus/ui/proofprintdialog.{h,cpp}` (untracked, already referenced from
both `add_executable` blocks in `scribus/CMakeLists.txt`), plus `scribus.cpp`,
`scprintengine_ps.cpp`, `scribusstructs.h` (`PrintOptions::proofMedia` /
`::inputSlot`), `util_printer.{h,cpp}`.

How it decides what to show:

- **Printer** — `PrinterUtil::getPrinterNames()`. Note this lists more queues
  than `lpstat -a` (discovered network printers too).
- **Paper / Source** — `lpoptions -p <queue> -l`, i.e. **cupsd, not the PPD
  file**: `/etc/cups/ppd` is `root:lp 0640` and the desktop user is not in group
  `lp`, so parsing the PPD would fail for exactly the users who need it. The
  queue's default is the token marked `*`.
- Paper is filtered to A4/A3 plus the queue's own default, since those are the
  only sizes a broadsheet proof is scaled onto. One entry left ⇒ **no combo at
  all**, just a fixed label. Source is hidden entirely when the queue reports one
  slot.
- **Per-printer memory** in the `proof_print` prefs context, keys
  `Paper_<queue>` / `Source_<queue>` plus `LastPrinter`, so an A3 machine
  remembering A3 cannot change what an A4-only machine does. Production print
  settings are never touched.

Gotchas worth keeping:

- `QFormLayout::addRow(QString, QLayout*)` **cannot set a buddy**, so the label
  rendered the literal `P&aper:` and Alt+A did nothing. Build that label by hand
  with `setBuddy()`. The rows that pass a *widget* are fine.
- Empty `Paper_<queue> = ''` entries appear for every queue merely scrolled past
  in the combo: `PrefsContext::get(key, default)` **inserts the default when the
  key is missing**. Harmless (empty ⇒ fall back to the queue default) but it is
  why the live prefs looked like the memory was broken when it was not.
- Enter must print from anywhere in the dialog: a combo or the spin box
  otherwise swallows Return, so there are explicit `QShortcut`s for
  Return/Enter, and the standard `QDialogButtonBox` Ok button is relabelled
  "Print" rather than adding a custom AcceptRole button (an added button loses
  default status when the dialog is shown).

**Still open:** PS-side form reuse for bullets (≈1.6% of a 5 MB proof — measured
as not worth it so far), and the **paper-unknown fallback has still never been
exercised** — detection has succeeded on every queue tried.
