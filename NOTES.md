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
| Styled copy/paste (Ctrl+Shift+C / Ctrl+Shift+V ⚠ — the V half only became true in `e2e218f`) | `8494cbf` | |
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
| **Auto Arrange Frames** — **PARKED**: selection-scoped compaction + dry-run dialog built and validated on a synthetic page, wrong on a real broadsheet. Unreachable from the UI; code kept compiled. `autoarrangeengine.{h,cpp}` + plan dialog | `c58f023` ⚠, fenced `2cb46b0`, rewritten `f577c19` | |
| Text-frame edge-resize band; overflow icon off the corner | `e6b0cf0` | |
| Overflow-click on an empty page creates a source-styled linked frame | `e03d7f6` | |
| Double-click drills into groups (text frames straight to edit) | `698507b` | |
| Faircode Frames shipped scrapbook — 23 templates in 4 waves | `c117e41`, `83e7e0b`, `957c424`, `181081d`, `d45958d` | |

⚠ Auto Arrange landed inside a commit titled *"add the image editor sources under
version control"* (2026-07-28, a sweep of several untracked files), so
`git log -- scribus/autoarrangeengine.cpp` returns one commit whose message says
nothing about auto-arrange. Search by symbol, not by commit message.

⚠ **Auto Arrange is PARKED and unreachable from the UI — but not abandoned.**
Dropping stories and images roughly onto the page and tidying them in one action
is a core part of the operator's workflow, so this is worth returning to. It has
been through two engines: the original destroyed a production broadsheet, and
its selection-scoped replacement is right on a synthetic page but wrong on a real
one. The whole history below is kept deliberately — it is the list of ways this
feature can go wrong, and the next attempt should start by reading it.

The **original** engine (`c58f023`, now replaced) did not compact frames in place
— it *re-laid out* the page, discarding every position and re-stacking each
column from `area.top()`. Three structural faults, recorded because they are the
failure modes any future whole-page attempt must avoid:

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

Undo was *not* the problem, then or now: the run is wrapped in one
`UndoTransaction` and the geometry setters record through
`checkChanges()`/`moveUndoAction()`, so one Ctrl+Z reverts the geometry. (Under
the old engine, text redistributed across a linked chain might not come back;
the new one never resizes, so no reflow can occur.)

**PARKED as of 2026-08-07.** The selection-scoped engine is *correct on a
synthetic page* — plan, apply and single-step undo all verified end to end:

```
plan     StoryB 150.0,320.0 -> 150.0,182.0    StoryC 150.0,520.0 -> 150.0,274.0
applied  StoryA y=70.0  ImgA y=90.0  StoryB y=182.0  StoryC y=274.0
undone   StoryA y=70.0  ImgA y=90.0  StoryB y=320.0  StoryC y=520.0
```

confirming containment (the image is absorbed into StoryA's block and never
listed), topmost-block-untouched, the 12 pt arithmetic, X never being written,
and one Ctrl+Z restoring everything.

**On a real broadsheet it was wrong: the blocks were wrong — it split one story
into two.** That is enough to name the cause. Containment only merges a frame
that lies *entirely inside* another. It handles a story built as one body frame
with the image, caption and headline nested inside it — which is how the sample
page happened to be built — but **a headline sitting directly ABOVE its body, or
an image above its caption, is adjacent, not contained, so the story splits.**

Checked against the sample page: three pairs are stacked directly above a frame
of identical width with a small gap and are *not* in a containment relation —

```
Copy of u148 (w  87.9) ABOVE Copy of u148 (w  87.9)   gap 10.1   width diff 0.0%
971162620    (w 340.1) ABOVE 1494689009   (w 340.1)   gap  3.8   width diff 0.0%
738728463    (w 219.7) ABOVE 1654643222   (w 219.7)   gap  7.8   width diff 0.0%
```

Only the third survived, and only because both frames happened to sit inside a
larger story frame that merged them indirectly. The rule was fitted to a page
whose stories nest; most pages do not.

**Direction for the next attempt** (not implemented): add a bounded adjacency
rule alongside containment — merge P and Q when Q sits directly below P with a
gap under ~20 pt *and* their widths match to within ~10%. That is far tighter
than the plain-overlap rule that chained a page into mega-blocks, because it
demands near-equal width as well as adjacency.

⚠ Its known ambiguity, which the single sample cannot settle: **two separate
one-column stories stacked with a small gap look identical to a headline above
its body.** Adjacency would wrongly fuse them. Resolving that needs either more
before/after pairs, or a signal beyond geometry — a paragraph style that marks
headlines, or the operator grouping each story explicitly. Do not guess at it.

So the feature is unreachable again: `defKeys` binds no shortcut, and the
Item-menu and context-menu entries are gone. **Everything else is kept and
compiled** — `autoarrangeengine.{h,cpp}`, the dry-run `AutoArrangeDialog`, and
the derivation tools in `~/scribus-crashlogs/tabletest/` (`sla_layout_diff.py`,
`arrange_model.py`). The action is still listed in Preferences → Keyboard
Shortcuts, so binding a key by hand is all it takes to resume testing. None of
this is wasted; it is the starting point when more samples arrive.

⚠ **A shortcut saved in an existing `scribus172.rc` overrides the compiled
default**, so clearing `defKeys` alone does not disarm an existing profile — the
saved line has to be removed from the file too, with Scribus closed, because it
rewrites its prefs on exit.

**Whole-page arrangement is DEFERRED pending more samples. Do not fit a
heuristic to one page.** Diffing the operator's before/after pair produced a
validated *block* rule but no defensible *placement* rule. A global
"close every vertical gap to 12 pt" moved **9 of 11 blocks** where the operator
moved 3, relocating frames that had deliberately been left alone — one had a
**445 pt** void above it and was not touched. The operator confirmed the sideways
moves, and two further ones, were **editorial placement, not mechanical
compaction**. Strip those and roughly one move remains to fit a rule to. One
sample cannot determine a whole-page algorithm, and guessing at one is exactly
what destroyed the broadsheet. More before/after pairs are being collected; the
whole-page version gets revisited only if a consistent rule appears across them.

**What shipped instead: selection-scoped compaction.** The operator selects the
frames to tidy; nothing unselected can move. Editorial judgement stays with the
operator, the tool only does the arithmetic. Combined with dry-run-first, the
catastrophic failure mode is gone by construction — the engine writes only Y,
only for frames in the selection, and only after the moves have been displayed
and accepted.

**The block rule (validated, reusable).** Derived from the pair and confirmed
against ground truth — all 3 hand-moved blocks reproduced exactly:

* a block = a frame plus everything **fully contained** in it (image, caption,
  group, headline-over-body)
* plus a **hairline rule** immediately above whose width matches the block's to
  within 5%. In the sample the story's own rule matched exactly (456.1 vs 456.1,
  gap 9.2 pt) while a page-spanning divider 15% wider (797.8 vs 696.1, gap 14.0)
  belonged to something larger and correctly stayed put.
* ⚠ use **containment, not overlap**. Plain overlap chains transitively — A
  touches B, B touches C — and fused a dense page into blocks of 6 and 5 where
  the real ones were 4, 3 and 2. Containment forms a tree and cannot chain.
* groups are *members*, never the definition: **0 of 13** moved frames were
  group children, while **27** grouped frames never moved.

Reference implementation and the tools that derived all this live in
`~/scribus-crashlogs/tabletest/`: `sla_layout_diff.py` (per-frame deltas,
co-moving units, grouping evidence, grid clusters) and `arrange_model.py` (the
rule in Python, scored against the hand-tidied page). Validate rule changes
there before touching C++ — it is far cheaper than a build cycle.

**The original specification as described** (kept for reference; the diff
showed it is only partly mechanical):

* each news item — text frame plus its image frame(s) — moves as ONE block
* blocks snap to the column guides and stack top-down, closing vertical gaps
  with consistent gutters
* frames spanning multiple columns keep their span
* the advertisement keeps its position and size
* reading order and column region are preserved; nothing crosses into a column
  it did not already occupy

The block-identification question — explicit groups vs. spatial containment — is
answered from the diff, not assumed: `~/scribus-crashlogs/tabletest/sla_layout_diff.py`
reports which frames share an identical delta (so travelled as a unit) and
whether those members are group children, chained frames, or neither.

**State:** the engine is rewritten (`autoarrangeengine.{h,cpp}` — `planForSelection()`
/ `applyPlan()`, no `ArrangeOptions`/`ArrangeResult` any more) and
`AutoArrangeDialog` is now a dry-run reporter: it lists every planned move as
(block, frame, type, from, to) and writes nothing unless Apply is pressed.
Changing the gutter recomputes the plan in place. `applyPlan()` writes **Y
only** — X is never touched, because horizontal motion is what made the old
engine destructive — as one undo transaction.

**Remaining:** operator verification. The menu entries and Ctrl+Shift+F stay off
until then, so the only way in is to bind a key under Preferences → Keyboard
Shortcuts (the action is deliberately still listed there). Once verified: restore
the Item-menu and context-menu entries and the default shortcut, and drop the
note above them in `scribus.cpp` / `contextmenu.cpp`.

### UI and panels

| Feature | Key commits | Branch |
|---|---|---|
| SuneerControlBar — text/image/table control toolbar; live font preview, gap spins, border and padding controls | `17c7747`, `7520912`, `5270cfd` | |
| Proof Print button on the control bar | `a537c5a` | print-proof |
| Paragraph Styles panel — News Browser as a third tab, in-app help, dark-theme readability, Next Style chain icon | `a0dbf1b`, `39d7126`, `a3090b2`, `da7ef2a`, `f757648`, `3236fe11`, `c4c2bf4` | |
| Column Style config → Design Style — per-config "Design Style" link applied after the columns, one undo step, missing-style status message, rename/remove follow-through, "Link Config N → style-N", export/import field. Verified by the user | `c0130ad` | feature/ctp-output |
| Paragraph Styles panel — per-style keyboard shortcut (right-click / ⌨ button), stored in `ParagraphStyle::shortcut()` (the Style Manager field, so in the .sla), conflict check with Replace/Cancel, export/import JSON. Verified by the user | `ea52dc3` | feature/ctp-output |
| Default workspace layout, Faircode splash, title-bar build stamp | `3590de5`, `f57058a`, `09c2b02` | |
| Paragraph Shading popup on the control bar — local override, never edits the style; embeds the Style Manager's `SMPShadeWidget`; `itemSelection_ResetParagraphShading()` ⚠ | `7a02607` | feature/paragraph-shading-popup |
| Stock toolbars start hidden on a new profile | `e8a34ae` | autofit-typography |
| Text Distances section hidden from content properties | `505a048` | |
| Control-bar keyboard navigation — Tab/Shift+Tab walk the bar, Enter applies, Esc cancels, both returning the caret | `3b1d012`, `e705ed0` | |

⚠ Three traps from making Tab work in the SuneerControlBar, each of which cost a
wrong diagnosis before being measured:

1. **An editable `QComboBox` emits `activated()` when it commits on focus-out.**
   `onFontChanged()` ended with an unconditional `canvas->setFocus()`, so merely
   *tabbing out of* the font field threw the caret back into the frame. Reading
   the code suggested Tab could not reach that slot; instrumentation showed it
   did. Focus is now returned deliberately, never as a side effect of a value
   change.
2. **Qt builds its tab-focus chain from parent/creation order**, which for a
   `QToolBar` full of parentless widgets added to layouts does not match reading
   order — and the composite selectors (`StyleSelect`, `AlignSelect`) are not
   focusable by default. Hence the explicit `setTabOrder` chain plus
   `Qt::StrongFocus`.
   ⚠⚠ **`setTabOrder()` is a no-op when EITHER widget is `Qt::NoFocus`, and the
   block must run AFTER `addWidget(container)`.** Both bit us. Setting the focus
   policy and the tab order in one loop applied `StrongFocus` to `chain[i]` while
   `chain[i+1]` was still `NoFocus`, so the very first pair silently did nothing
   and Qt fell back to creation order — Tab walked the *line and image* controls.
   Two passes now: policy for every widget first, then order. And Qt rebuilds the
   focus chain on reparent, so a block placed mid-constructor is discarded when
   the tree is parented into the toolbar; it lives at the end of the constructor.
   The failure is silent in both cases — the chain simply isn't what you wrote,
   and it can look plausible because creation order also starts at the font
   field. Measure the real order (log `QEvent::FocusIn`) rather than assume.
3. **An event filter installed only on the tab-order chain misses focusable
   widgets outside it.** Esc appeared to be "consumed by the filter" because the
   frame stayed in edit mode; it was actually reaching *neither* the filter nor
   the canvas, because focus had landed on a control the filter was not on. The
   filter now covers every descendant of the bar (excluding the shading popup,
   which owns Escape) and tests ancestry.

⚠ **Three STOCK labels are renamed locally** in `actionmanager.cpp` (~1681):
`editCopyContents`, `editPasteContents`, `editPasteContentsAbs` were `&Copy`,
`&Paste`, `Paste (&Absolute)` — identical strings to `editCopy`/`editPaste`.
That reads fine inside the **Edit → Contents** submenu, but Preferences →
Keyboard Shortcuts flattens the menu path away, so the list showed "Copy" and
"Paste" twice each with nothing to tell them apart. They are now
`&Copy Image Contents` / `&Paste Image Contents` /
`Paste Image Contents (&Absolute)`; mnemonics stay C/P/A, distinct within the
submenu. Menu and shortcut editor cannot diverge — both read the same
`setTexts()`. Expect these three to conflict on any upstream rebase, and note
the 75 shipped `.ts` files still carry the old source strings, so those entries
orphan and fall back to English in a translated UI (no `.qm` is built here and
the operator runs English, so no practical effect today).

⚠ **`enforceClipboardShortcuts()` (`scribus.cpp`) re-asserts the styled
clipboard chords on every startup, so a wrong target there is self-healing in the
WRONG direction.** It bound `Ctrl+Shift+V` to `editPasteOriginalPosition` rather
than `editStyledPaste`, and exempted it from the loop that strips those chords
from every other action — so Styled Paste had no shortcut at all while Styled
Copy had `Ctrl+Shift+C`, and clearing the stale binding by hand would not stick.
Fixed by pointing both the assertion and the exemption at `editStyledCopy` /
`editStyledPaste`; `editPasteOriginalPosition` is now unbound by default
(operator's decision — rebindable from Preferences).

Note this looked like "duplicate Copy/Paste entries" in Preferences → Keyboard
Shortcuts. It was not: **no action is registered twice**, each appears once in
`defMenus` and `defKeys`. Five distinct actions simply have confusable labels —
`&Copy`, `&Paste`, `Paste in Original Position`, `Styled Cop&y`, `St&yled Paste`.
Count registrations before hunting for a duplicate-registration bug.

⚠ Two naming traps in that toolbar, each of which cost a round: **"font style"
is `m_styleCombo`** (tooltip "Font Style", items Regular/Bold/Italic), NOT
`m_styleSelect`, which is the underline/strikethrough/superscript effect buttons;
and **`m_trackingSpin` is character tracking, not leading** — line spacing is
`m_lineSpSpin`. The tooltips are the only reliable discriminator; the variable
names are not.

⚠ **The text caret does NOT need re-arming when focus returns to the canvas.**
It is a blink timer owned by `CanvasMode_Edit`, armed in `activate()`
(`canvasmode_edit.cpp:452`) and stopped in `deactivate()` — both driven by
**mode** transitions, not focus. Measured across a focus excursion into the
toolbar: `appMode` stays `modeEdit`, `deactivate()` never runs, and the cursor
position is unchanged (41 before, typing lands at 41 after). `canvas->setFocus()`
alone restores typing. Do not add `requestMode(modeEdit)` calls to "fix" this.

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

### Image eraser (non-destructive, on-canvas)

| Feature | Key commits | Branch |
|---|---|---|
| `EF_ERASERMASK` mask model, `modeImageEraser` canvas mode, options bar, all four renderers | (this change) | feature/ctp-output |
| Contour detection from the eraser mask (`util_contour`, `ScContour`) feeding `ContourLine` | (this change) | feature/ctp-output |

Paint-to-erase on a placed image, Photoshop style. Soft brush with a hardness
slider, Alt to un-erase, `[`/`]` to resize, Shift+E / mode toolbar to enter.

**The mask rides in `PageItem::effectsInUse` as `ImageEffect::EF_ERASERMASK`
(code 31), with a base64 PNG in `effectParameters`.** That choice is the whole
design, and it is worth understanding before changing anything here:

- `.sla` save/load is *generic* over effect codes
  (`scribus171format_save.cpp` writes `Code`/`Param`, `scribus171format.cpp`
  reads them back), so persistence needed **no file-format changes at all** —
  and copy/paste works for free, since that path serialises to SLA XML.
- Undo reuses `ScOldNewState<ScImageEffectList>` + `"APPLY_IMAGE_EFFECTS"`,
  already replayed by `PageItem::restoreImageEffects`. New labels
  `Um::EraseImageArea` / `Um::RestoreImageArea`.
- `PageItem`'s copy constructor already copies `effectsInUse`.

**The effect is a data carrier, not a transform. `ScImage::applyEffect` must
never act on it** — it is a chain of `if (code == ...)` tests, so an unhandled
code is skipped, and that is deliberate. On the export paths ScImage is CMYK,
where **`qAlpha()` is the black plate, not alpha** (`scimage.cpp:2580`, and the
interleaved writer at `:2614`). Writing the mask into alpha there would knock
holes in the K separation of every printed page. Each renderer therefore
composites the mask itself:

| Renderer | How the mask gets in | Verified |
|---|---|---|
| Canvas — `PageItem_ImageFrame::DrawObj_Item` | `imageForDraw()` returns a cached ARGB composite | yes, screenshot |
| PDF ≥1.4 / X-4 — `pdflib_core.cpp` | 8-bit `/SMask`, `mergeIntoAlphaBytes` | yes, byte-identical |
| PDF 1.3 / X-1a / X-3 — `pdflib_core.cpp` | 1-bit `/ImageMask`, `mergeIntoPdfImageMask` | yes, erased fraction matches |
| PostScript — `pslib.cpp` (2 sites) | ImageType 3 + InterleaveType 1, `mergeEraserMaskForPS` | yes, byte-identical |
| `scpageoutput.cpp` | alpha, RGB modes only | **no — Windows-GDI-only path** |

`ScPageOutput` is reachable only from `scprintengine_gdi.cpp`, which
`CMakeLists_Sources.txt` builds under `if(WIN32)`. It is dead on Linux, and its
CMYK case is deliberately left un-erased rather than risking the K plate.

**PDF/X-1a cannot carry transparency** (`PDFVersion::supportsTransparency()`
lists only 1.4/1.5/1.6/X-4). The newspaper preset is X-1a, so a feathered erase
quantises to a hard edge there. Erasing still happens; only the feather is lost.

#### Contour from the eraser mask

`Item > Shape & Paths > Detect Contour from Image` (Ctrl+Shift+K, also a
button in the Properties palette's Shape tab) traces the still-visible part of
a photo into the item's `ContourLine`, so text wraps the actual shape.

`util_contour.cpp` does the tracing. It is edge-following marching squares:
every cell edge between a solid cell and a hole becomes a directed unit edge
oriented with the solid side on the right, and chaining them gives closed rings.
Two things fall out of that orientation for free - **outer rings come out with
positive shoelace area and holes negative**, so containment never has to be
tested, and the saddle case has one rule: take the sharpest clockwise turn,
which keeps diagonally touching regions apart rather than tracing a figure
eight. Simplification is Ramer-Douglas-Peucker; Scribus had none of its own
(nothing in `util_math.cpp`, `fpointarray.cpp` or lib2geom).

Traps met on the way:

- **`restoreContourLine`'s redo branch hardcodes `ContourLine = PoLine.copy()`.**
  That is right for "reset contour to frame shape" and wrong for anything that
  computes a contour, so this uses its own `DETECT_CONTOUR` state holding an
  old/new `FPointArray` pair. The wrap-mode change rides in the same state:
  one user action must undo as one step.
- **Text wrap flattens a contour to a single `QPolygon` and ignores the
  sub-path list.** `pageitem.cpp:10038` calls `flattenPath(ContourLine, Segs)`
  and hands the polygon to `QRegion` without looking at `Segs`. Holes still
  work, because the concatenation makes a keyhole polygon and `QRegion`'s
  odd-even fill reads that correctly - verified visually, text flows inside an
  erased hole. Do not assume sub-paths are honoured anywhere else on that path.
- **Wrap only applies to items above the text frame in z-order.** An
  apparently-not-working contour is usually this, not the contour.
- **A detected contour does nothing until the item's text flow is set to use
  it**, which is why the dialog has a "Use the contour for text wrap" box on by
  default rather than leaving the feature looking inert.
- `svgPath()` writes coordinates through `QString::number`, i.e. six
  significant digits, so a contour is only bit-stable *after* its first save.
  save -> load -> save is byte-identical; in-memory doubles vs reloaded ones
  are not necessarily.

Unit tests: `scribus/tests/contourdetecttests.cpp`, 13 cases - square gives 4
nodes, L-shape 6, holes come back with opposite winding, diagonal cells stay
separate, plus threshold/mode/empty/opaque handling.

One caution about testing simplification: a metric that measures how far the
ring's *vertices* sit from the ideal shape cannot fail, because RDP only ever
keeps points that were already on the traced boundary. The first version of the
circle test did exactly that and read ~0.6 px at every tolerance from 0.5 to 8.
What the tolerance bounds is how far the *chords* sag, so the test samples
along each edge.

Unit tests: `scribus/tests/erasermasktests.cpp`, 16 cases covering the codec,
the brush profile, both export merge formats, and the two stroke regressions.

```bash
cmake -S . -B build-tests -DWITH_TESTS=ON
cmake --build build-tests --target erasermasktests
QT_QPA_PLATFORM=offscreen build-tests/scribus/tests/erasermasktests
```

Build the target by name, not `all`: **`scribus/tests/` is bit-rotted**. Its
`TESTS_LIBRARIES` was built from the Qt4-era `QT_QTTEST_LIBRARY`/`QT_LIBRARIES`,
which are empty under Qt6, so nothing in that directory linked — now pointed at
`Qt6::Test`, which also fixed `cellareatests`. `scribus_tests_lib` still does not
compile (`runtests.cpp` cannot find `QTest`; `testStoryText.cpp:17` has an
ambiguous `insertChars` overload). Both are pre-existing and were left alone, so
`WITH_TESTS=ON` still fails on `all`. The main build is configured
`WITH_TESTS=OFF` and is unaffected.

### Object removal (inpainting)

| Feature | Key commits | Branch |
|---|---|---|
| `util_inpaint` — Telea fast marching kernel, standalone + unit tested | `16ea33e` | feature/ctp-output |
| `modeRemoveObject` canvas mode, red mask overlay, options bar, progress dialog, apply/undo flow | `4af4715` | feature/ctp-output |
| Criminisi exemplar filling, and choosing between the two per mask | (this change) | feature/ctp-output |

Paint a red mask over something unwanted in a placed photo, press Apply, and the
covered pixels are rebuilt from the rest of the picture. Shift+R, the mode
toolbar, or `Item > Shape & Paths > Remove Object`.

**This is not the eraser, and confusing the two will lead you to the wrong
layer.** The eraser hides pixels behind a mask that lives in the `.sla` and can
be taken back pixel by pixel; nothing about the picture changes. This invents
new pixels, writes them to a new PNG in `.scribus_edits/` beside the document,
and points the frame at that file. Its mask is scratch state, never saved.

#### Two methods, because one method cannot do both jobs

| | fast marching (Telea 2004) | exemplar (Criminisi 2004) |
|---|---|---|
| what it does | weighted average of nearby known pixels, worked inwards | copies the best-matching patch of real picture, worked inwards |
| reconstructs | colour, gradient | colour, gradient, **texture, edges** |
| good for | wires, aerials, scratches, dust, anything against flat colour | people, clothing, crowds, foliage, any busy background |
| cost, 400x1000 mask on a 4000x3000 photo | 0.2 s | 4 s |

`Method::Auto` chooses, and callers should leave it alone. The rule is in
`Inpaint::inpaint()`:

- **mask thickness <= 4 px → fast marching.** Thickness, not area: a scratch a
  hundred pixels long and two wide has every one of its pixels within a pixel or
  two of real data, so the averaging has no room to flatten anything.
- **median ring gradient < 3 → fast marching.** Sky, a backdrop, a wall. No
  texture to reproduce, a diffusion is exactly right, and it is much cheaper.
- **otherwise → exemplar.**

Both statistics come from `analyseMask()`, off one shared chamfer distance
transform. The ring statistic is a **median** over a **narrow ring**, and both
of those were mistakes first: the mean over the padded bounding box read 6.5 on
a crowd and classified it as flat, because a tall mask's bounding box is mostly
far-away picture and a mean is dragged about by a handful of strong edges. The
median of a true ring reads about 0.5 in open sky and 4 to 6 in a crowd.

#### Why the diffusion had to be replaced for wide masks

Reported as "the removed region is visibly detectable, blurred and smeared".
Measured on a crowd photograph, 141x191 mask, before any of this change:

```
telea radius  3   texture ratio 0.50    PSNR 13.29 dB
telea radius  5   texture ratio 0.41    PSNR 13.68 dB
telea radius  9   texture ratio 0.33    PSNR 14.11 dB
telea radius 15   texture ratio 0.26    PSNR 14.65 dB
```

("Texture ratio" is mean gradient magnitude inside the fill over the same in a
ring of untouched picture around it. 1.0 means the fill carries as much detail
as its surroundings; the ground truth scores 0.95.)

Three things are visible in that table and all three matter:

- **The fill carried 41% of the detail of its surroundings.** That is the
  smooth blob a reader spots.
- **Gradient energy collapses with depth**: 16.5 at the mask edge, then 12.2,
  11.1, 10.7, 10.7 going inwards, against a ground truth that stays between 26
  and 35 throughout. Each filled pixel is an average of pixels that were
  themselves averages, and an average has less detail than its inputs. That is
  a property of *any* diffusion, not a tuning problem - which is why the fix
  had to be a different method rather than better weights.
- **PSNR rises as the picture gets worse.** More blur, better PSNR. Anyone
  tuning this kernel against PSNR will reintroduce the smear, so
  `testBlurringScoresBetterOnPsnr()` states that trap out loud and fails if
  someone quietly removes the texture measure.

After the change, the same mask: **texture ratio 0.85, PSNR 14.58, 0.2 s**.

#### The exemplar implementation, and four measured deviations

Criminisi's method is in `struct Exemplar`. Each round it picks the front pixel
with the highest confidence x data term - confidence being how much of the patch
is real photograph rather than earlier guesswork, the data term how strongly an
edge runs into the hole there, so edges are continued before flat areas are
touched - then copies in the best matching patch of real picture. Copied, never
averaged, which is the whole difference.

Everything below was added on measurement, not taste:

- **Reconstructions are never sources.** Only never-masked pixels may be copied
  *from*. They take part in *matching*, weighted by confidence, but a guess can
  never be laundered into evidence for the next guess.
- **Source usage penalty** (`SourceUsagePenalty`). Exemplar filling repeats
  itself: it copies a patch, that patch becomes the context the next match is
  judged against, so it matches the same place again, and a recognisable object
  gets stamped across the hole several times. Charging a source area for each
  use broke the loop: PSNR 13.74 → 14.31, texture ratio 1.18 → 1.12.
- **The compared window is wider than the copied one** (`matchR = patchR + 3`).
  Matching over exactly the block being copied loses the *phase* of a repeating
  pattern completely - on a regular grid only 56% of the fill came back in step,
  which is worse than always guessing dark. Widening it fixed that (95%).
  **But only the copied block has to be untouched picture**; requiring the whole
  compared window to be untouched pushes the search out to distant patches of
  the wrong material and cost 1.7 dB on the crowd photograph. Non-original
  pixels in the compared window are skipped instead, with a coverage floor
  (`MinMatchCoverage`) so nothing wins on a sliver of evidence.
- **Seam softening** (`softenSeams()`). Patches are copied whole and butt
  against each other; the step where two meet belongs to neither piece of
  photograph. Only pixels actually on such a join are touched, once, so the
  texture inside each patch survives - blurring the whole fill would undo the
  entire point. PSNR 14.31 → 14.38, texture ratio 1.12 → 1.06.

Speed comes from three places: a summed-area table over the untouched region so
"is this whole candidate real picture?" is one subtraction; early exit on the
running SSD; and a coarse sweep whose step grows with the search window,
refined at full resolution around the winner. That last one was worth 3 to 4
times on its own - a large removal from a newspaper photograph went from 30 s
to 10 s, a person-sized one from 14 s to 4 s, with the crowd measurements
unchanged to within 0.3 dB.

#### What it still cannot do

It does not know what was behind the object, and nothing here can invent a face
or a limb that was never photographed. What it produces is *plausible picture
from elsewhere in the same photograph*, which reads as a photograph rather than
as a hole. On a complex subject it will still duplicate recognisable things and
leave patchy joins - visible if you look for them, which is a different and much
weaker failure than the smear it replaced. Small and medium removals against
texture are where it is genuinely good.

#### Traps met on the way

- **`PageItem::loadImage()` resets image scale and offset whenever the file name
  changes** (`pageitem.cpp:10249`), so a hand-placed picture jumps in its frame
  when the frame is repointed. Fixed on both halves: the output PNG is written
  carrying the *source's* DPI, so `72.0/xres` lands on the identical scale, and
  the offsets are restored through the setters afterwards. Going through the
  setters matters - they record their own undo states inside the transaction, so
  **redo** puts the placement back too, which stuffing the values into the
  members would not.
- **The progress dialog is modeless, so the finish handler may not trust
  anything.** It re-finds the frame by name through `getItemFromName()` and
  refuses to apply a result if the document, the frame, or the frame's `Pfile`
  changed while the worker was running.
- **The worker can outlive the mode.** It talks through
  `shared_ptr<atomic<bool>>` / `<atomic<int>>` rather than cross-thread signals,
  and captures its images by value, so closing the document mid-run cannot leave
  it writing into freed memory. The mode's destructor sets the cancel flag and
  takes the dialog down, which is otherwise parented to the main window and
  would be stranded on screen.
- `QDialog::reject()` (Escape) does not go through `closeEvent()`, so it needed
  overriding separately or Escape would hide the dialog and leave the job going
  with nothing on screen to stop it.
- **Do not read the hole.** Under the mask the picture still contains the object
  being removed. Every gradient, every match sample and every average in both
  methods is guarded on "has this pixel got a colour yet", or the thing being
  removed steers its own removal.

#### The second fill: a model on the other end of a socket

| Feature | Key commits | Branch |
|---|---|---|
| AI Services preference page, `AIInpaintService` / `LamaInpaintService` | `764fceb` | feature/ctp-output |
| Mock-server tests for the client | `4a3e7c3` | feature/ctp-output |
| Apply (Fast) / Apply (Best Quality) in the options bar | `e49ec90`, `8ffffae` | feature/ctp-output |

Apply (Fast) is the built-in kernel above, unchanged. Apply (Best Quality)
sends the neighbourhood of the mask to a local **IOPaint** server running LaMa,
which the user installs and runs themselves:

```bash
pip install iopaint
iopaint start --model=lama --port=8080
```

Nothing in Scribus installs, downloads or starts it, and the feature is
invisible until `Preferences > AI Services > Enable AI features` is ticked. It
contacts the address in that preference and no other: no fallback host, no
discovery, no telemetry.

**The wire format was established by asking a running instance, not by reading
anything**, and it is not what the shape of the task suggested - it is not
multipart:

- `POST {url}/api/v1/inpaint`, `Content-Type: application/json`, body
  `{"image": "<base64 PNG>", "mask": "<base64 PNG>"}`. A `data:image/png;base64,`
  prefix is tolerated but pointless.
- **White in the mask means "regenerate this"** - the same sense the built-in
  kernel uses, so nothing anywhere has to invert it.
- Success: `200`, `Content-Type: image/png`, the whole repaired picture as raw
  PNG bytes, same size as sent, unmasked pixels bit-identical.
- Failure: `500` with JSON carrying the real reason in `errors` (with `detail`
  and `error` alongside). That text is what the user is shown, rather than a
  status code.

`testRequestWireFormat()` in `lamainpaintservicetests.cpp` pins all of it. If
IOPaint changes the contract, that test says so rather than a user wondering
why nothing happens.

Things worth knowing before changing any of it:

- **The socket, the PNG encoding and the base64 both ways run on a private
  thread.** Encoding a 2048-pixel picture is a few hundred milliseconds on its
  own. `QNetworkAccessManager` belongs to the thread that created it, so the
  worker owns it and the public methods are queued invocations.
- **Cancelling is answered.** It arrives as `inpaintFailed()` carrying
  `AIInpaintService::cancelledMarker()`, so no caller waits for a signal that
  never comes, and no caller shows it to a user as an error. The request is
  genuinely aborted - the mock test counts connections dropped before it
  answered, so it fails if the reply were merely ignored.
- **A timeout and a user cancel both surface as an aborted request** and have to
  be told apart deliberately, which is why there is an explicit `QTimer` rather
  than `setTransferTimeout()`.
- **Only the neighbourhood of the mask is sent**, padded generously - a model
  fills a hole from what surrounds it, so a tight crop starves it - and capped
  at 2048 pixels on the longest side. **Only the masked pixels of what comes
  back are kept**, so the rest of the picture stays bit-identical instead of
  returning softened by a round trip through a scaler.
- The undo step is `Um::RemoveObjectAI` and the file is tagged `_lama_` rather
  than `_inpaint_`, so which engine produced a given result is visible both in
  the undo history and in the folder.
- Everything careful about applying a result - is this still the frame that was
  asked about, is the destination still not the user's original, write before
  moving anything, one undo step - lives in `deliverResult()` and both paths go
  through it.

**Trap: a widget in a QToolBar is owned by the QWidgetAction that
`addWidget()` returns**, and it is the *action's* visibility the toolbar lays
out from. Hiding the "Large area" label itself looked right, because the label
was correctly absent, and then it never appeared for a mask that should have
had it (`8ffffae`).

Measured on a real IOPaint running LaMa on CPU: connection test 6 ms, a
100x100 fill 525 ms, a person removed from a 756x248 crowd photograph in a few
seconds end to end. That last one is genuinely clean - the railing, the sea,
the hedge and the grass all continue - which is the case the built-in method
is weakest at.

Unit tests: `scribus/tests/lamainpaintservicetests.cpp`, 14 cases, all against
an in-process `QTcpServer` mock. Qt's `QHttpServer` module is not present in
this build.

#### The third fill: OpenRouter, a paid cloud API

| Feature | Key commits | Branch |
|---|---|---|
| `OpenRouterInpaintService`, `AIInpaintServiceFactory`, provider dropdown | `7339087`, `2f0adf4`, `903b940` | feature/ctp-output |

One API key reaches 48 image models across Google, OpenAI, Black Forest Labs,
ByteDance, Recraft and others, so a model that refuses a particular edit is
answered by picking another one in Preferences rather than by integrating
another vendor. `Preferences > AI Services > Provider` chooses; the removal
mode asks `AIInpaintServiceFactory` and never names an implementation.

**The wire format was read off the live API on 29 Aug 2026, and three things
a reasonable person would guess are wrong.** The task this was built from
guessed all three, which is why they are written down:

| Guess | Actually |
|---|---|
| `reference_images: ["<base64>"]` | `input_references: [{"type": "image_url", "image_url": {"url": "..."}}]` |
| `"modalities": ["image"]` | no such field on this endpoint |
| `GET /api/v1/auth/key` | `GET /api/v1/key` - `/auth/key` does not exist |

- `POST https://openrouter.ai/api/v1/images`, body `{model, prompt,
  input_references}`. The reference URL may be `http(s)` or a `data:` URL, so
  the picture goes inline and is never uploaded anywhere first.
- Success: `200` with `{"created", "data": [{"b64_json", "media_type"}],
  "usage": {..., "cost": 0.04}}`. `usage.cost` is the real charge in USD for
  that one call.
- Failure: `{"error": {"code", "message", "metadata"}}`. A moderation refusal
  carries `reasons`, `provider_name` and `model_slug` in the metadata.

**Sources**: <https://openrouter.ai/docs/guides/overview/multimodal/image-generation>,
<https://openrouter.ai/docs/api-reference/errors>, and the machine-readable
<https://openrouter.ai/api/v1/images/models>, whose `supported_parameters` is
what settled the field names.

Things worth knowing before changing any of it:

- **None of these models takes a mask channel.** LaMa gets an image and a
  separate mask; these get a picture and an instruction. So the mask is said in
  the only language they all read: painted onto the picture as a bright red
  overlay (`255,30,30` at alpha `220`), JPEG q92, with a prompt that explains
  what the red means. That is why one reference image is sent, not two.
- **The images API has its own catalogue.** `/api/v1/images/models` lists 48;
  the chat-completions list at `/api/v1/models` is a different and much smaller
  set that does not contain most of them. Checking a model id against the wrong
  one will tell you it does not exist. `black-forest-labs/flux-dev` genuinely
  does not - the FLUX models here are the `flux.2-*` family.
- **The API root is pinned at compile time.** It is a constructor argument only
  so the unit tests can point at an in-process mock; `AIInpaintServiceFactory`
  never passes it, which is what makes "a picture only ever goes to
  openrouter.ai" a property of the program rather than of a setting. For the
  same reason a result handed back as a remote `url` is refused rather than
  fetched.
- **The key travels in the `Authorization` header and nowhere else.** There is
  no logging in `openrouterinpaintservice.cpp` at all.
  `testApiKeyNeverLeavesTheAuthorizationHeader()` sweeps the path, the query,
  the body, every other header and the user-facing error text.
- **A refusal is not a failure and is not retried.** It arrives as
  `inpaintFailed()` prefixed with `OpenRouterInpaintService::refusalMarker()`,
  carrying the model's own words. Nothing retries and nothing silently
  substitutes another model - which model runs stays the user's choice. The
  mask survives so another model can be tried against the same selection.
- **The key is base64 in `scribus172.rc`. That is obfuscation, not
  encryption** - it keeps the key off the screen and out of a grep and does
  nothing against anyone who can read the file. TODO: OS keyring.
- The undo step is `Remove Object (OpenRouter: <display name>)` and the file is
  tagged `_openrouter_<model>_`, so two models run over the same area can be
  told apart afterwards in both the undo history and the folder.
- `AIServicePrefs::provider` is an int on the preferences file format. **Do not
  reorder `AIProvider`**, and note that anything unrecognised falls back to
  LaMa - the local one, not the one that spends money.

Unit tests: `scribus/tests/openrouterinpaintservicetests.cpp`, 32 cases, all
against an in-process mock. Nothing in the suite contacts openrouter.ai, needs
an account or spends money.

**Not exercised**: no removal has been run against the real API with a valid
key, so no picture has actually made the round trip and no charge has been
observed. What *has* been checked against the live service is the key endpoint:
a deliberately invalid key returned `401` and surfaced as "OpenRouter rejected
the API key...", which is also what confirms `/key` is the right path. Content
refusals have never been seen from any model, because no real request has been
made.

#### The 402 that was reported as a 60-second timeout

`28c716c`. OpenRouter answered `402` in well under a second and Scribus said
"OpenRouter did not answer within 60 seconds".

Root cause: **nothing read the response until `finished()`, and that reply
never finished.** The head - carrying the status and the reason - had been in
the socket the whole time. The same hole swallowed a rejection that arrives
while the picture is still uploading: the connection breaks, the status
attribute goes with it, and a good 402 came out as "could not reach
OpenRouter".

The fix reads the response as it arrives: `metaDataChanged` for the status the
moment the head lands, `readyRead` to accumulate the body, and a 1.2 s window
to pick up an error message before reporting. A status learned from the head
survives a connection that breaks afterwards, and the timeout no longer claims
silence when a status has already been seen.

**The existing tests could not have caught this**, and neither can any test
whose mock sends a complete response and closes - every reply then reaches
`finished()` and every path is the happy one. `MockOpenRouter::stallAfterHead`
sends a head plus a truncated body and keeps the connection open, which is the
shape that reproduces it.

- **401 and 407 are a Qt limitation, not ours.** They are the HTTP
  authentication statuses and Qt withholds the *entire* response for them - no
  `metaDataChanged`, no `readyRead`, not even `authenticationRequired` - until
  the body is complete, in case it has to resend with credentials. A 401 whose
  body never completes gives the client nothing to act on and can only end at
  the timeout. Measured on Qt 6.8.2: every other status fired
  `metaDataChanged` within 3 ms, 401 and 407 fired nothing at all.
- **`lamainpaintservice.cpp` has the same shape** (`finished()` only, one
  `readAll()` at the end, status from the attribute) and was deliberately left
  alone - it talks to localhost, where a stalled body is far less likely.
  Worth fixing if it ever misbehaves.

### Google Gemini, the third provider

| Feature | Key commits | Branch |
|---|---|---|
| `GeminiInpaintService`, shared prompt + compositor, prefs, factory | `ce78d2c` `6fc0270` `29f9651` `63374f5` | feature/ctp-output |

Exists for a payment reason, not a technical one: OpenRouter needs a card that
works internationally, Google AI Studio takes UPI and bills in INR with GST.
The models are largely the same Nano Banana family.

- **The wire format is not the one most Gemini examples show.** Verified
  30 Aug 2026: Google moved image generation to the **Interactions API** and
  now labels `models/{id}:generateContent` the *Generate Content API
  (Legacy)*. It is `POST /v1beta/interactions` with the model as a *field*,
  and a flat `input[]` of typed parts
  (`{"type":"image","mime_type":...,"data":...}`) rather than nested
  `contents[].parts[].inline_data`. There is no
  `generationConfig.responseModalities`.
- **Model ids are the stable ones** - `gemini-3.1-flash-image`,
  `-flash-lite-image`, `gemini-3-pro-image`. The `-preview` suffixes they
  carried earlier in the year now 404. **OpenRouter's ids for the same models
  are different** (`google/…`, still `-preview`); the two catalogues are not
  interchangeable, and a test pins ours to Google's.
- **Two things only the live API could teach us** (`63374f5`), both found with
  curl and a deliberately invalid key:
  - **A bad key is `400`, not `401`** - `status: INVALID_ARGUMENT`, detail
    `reason: API_KEY_INVALID`. Keying off the number reported the commonest
    mistake there is as an unexplained bad request. Google's `status`/`reason`
    are now read first, the HTTP code only as a fallback.
  - **`POST /v1beta/interactions` wraps its error envelope in a one-element
    array**, while `GET /v1beta/models` sends it bare. The object-only parser
    dropped every message on the endpoint that does the work.
- Test Connection is `GET /v1beta/models`: a plain GET that runs no model, so
  it cannot be billed, and it still distinguishes a bad key from a project
  with no billing. The key goes in `x-goog-api-key` and never in the URL,
  though Google's own docs show `?key=` - a URL ends up in error strings and
  proxy logs.
- The reply handling was built with `28c716c`'s lesson already applied rather
  than repeating the bug.
- Shared with OpenRouter: the prompt (`ai/aiinpaintprompts.h`) and the red
  overlay plus JPEG encode (`ai/aiinpaintcomposite.h`). A user who switches
  provider over a payment method must not get different removals. The refusal
  marker moved up to `AIInpaintService`; OpenRouter's three statics remain as
  forwards.
- `AIProvider::Gemini = 2`. **Do not reorder `AIProvider`** - it is on the
  preferences file format.

Unit tests: `scribus/tests/geminiinpaintservicetests.cpp`, 31 cases, all
against an in-process mock.

**Verified by me**: the Preferences page on Xvfb - all three providers in the
dropdown, the Gemini box appearing and the other two hiding, the model hint
following the dropdown, and a full save/load round trip through
`scribus172.rc` (`Provider="2"`, `GeminiModel="gemini-3-pro-image"`, key
base64). The endpoint paths, the `x-goog-api-key` header and the error
envelope, against the live API with an invalid key.

**Not exercised**: no removal has been run against Gemini with a valid key, so
no picture has made the round trip, no charge has been observed, and **the
success-response parser has never seen a real success**. Content refusals and
`promptFeedback.blockReason` have never been seen from the real service.

### AI Text Tools — a second feature area

| Feature | Key commits | Branch |
|---|---|---|
| `AITextService` + shared HTTP base + prompt table | `9ca7c09` | feature/ctp-output |
| Claude / Gemini text / OpenAI clients, three mock suites | `9ca7c09` | feature/ctp-output |
| Prefs section, Item ▸ AI Text Tools menu, result + translate dialogs | `5d07502` | feature/ctp-output |
| Selection-driven menu enablement | `c85d7af` | feature/ctp-output |

**Deliberately not merged with `AIInpaintService`.** An image service takes a
picture and a mask and returns a picture that replaces pixels. This takes a
task name, some text, sometimes a picture, and returns writing a person then
judges. A model that writes a bad headline has still succeeded as far as HTTP
is concerned — the failure modes do not line up, and one interface serving both
would serve neither.

- **All three provider APIs had moved** since the feature was specified.
  Verified 30 Aug 2026:
  - **Claude** — `POST /v1/messages`, `x-api-key` **and**
    `anthropic-version: 2023-06-01` (not optional). Image is a content block
    with a **nested `source`**, and goes *before* the text (Anthropic's own
    guidance). `claude-sonnet-4-5` / `claude-opus-4-5` are superseded → Sonnet 5
    (default), Opus 5, Haiku 4.5.
  - **Gemini text** — **not** `models/{id}:generateContent`. Google moved text
    generation to the same **Interactions API** the image models use and now
    labels generateContent *legacy*. `POST /v1beta/interactions`, model as a
    field, flat `input[]` of typed parts. `gemini-2.0-flash*` are gone → 3.7
    Flash (default), 2.5 Pro, 3.1 Flash Lite. **Not the Nano Banana ids** — a
    test pins that no `-image` id appears in this list.
  - **OpenAI** — `POST /v1/chat/completions`, Bearer token, image as a complete
    **`data:` URL** inside `image_url`, and **`max_completion_tokens`** not
    `max_tokens`. The old spelling is *silently ignored* on current models, so
    only a test would notice it coming back. `gpt-5`/`-mini`/`gpt-4o` are gone →
    the 5.6 family (Terra default).
- **One HTTP base, three protocols.** The thread, the timeout-vs-cancel
  distinction, reading the response head as it arrives, and keeping the key out
  of URLs and messages live once in `AITextHttpService`. That set was learned
  from the OpenRouter 402 bug above; three copies would be three chances to
  reintroduce it. Each provider supplies ~40 lines: URL, auth header, body
  shape, answer path.
- **Every exit from `execute()` is asynchronous**, including the local checks
  that refuse before anything leaves. A caller that connects a slot and then
  calls `execute()` must not have that slot run inside its own call.
- **Costs are only quoted where a price was verified.** Claude's per-MTok rates
  are in the table and the estimate is real arithmetic (tested); Gemini's and
  OpenAI's text rates were not verified, so those report zero and the dialog
  omits the line. A made-up number on a line a user believes is worse than none.
- **Menu enablement is "a text frame", not "a text frame with text".** It is
  computed on selection change, and typing into an already-selected frame is not
  one — the stricter rule left the entries greyed until you clicked away and
  back. The emptiness check lives in `slotAITextTask()` instead. It also has to
  go in `AppModeHelper::enableActionsForSelection`, which runs *after*
  `HaveNewSel()` and re-enables the set it knows about; setting the actions
  before it silently undid them.

Unit tests: `claudetextservicetests` 26, `geminitextservicetests` 23,
`openaitextservicetests` 19 — all against the shared in-process mock in
`tests/aitextmockserver.h`. Nothing contacts a real provider. The test input is
Malayalam, because every document this exists for is, and each suite asserts it
arrives as Malayalam.

**Verified by me**: the Preferences section and its defaults on Xvfb; the
Item ▸ AI Text Tools submenu, its six entries and both enablement states; and
the handler reaching the factory and reporting "Enable AI features in
Preferences > AI Services" on a profile with AI switched off.

**Not exercised**: no real API call to any of the three. Claude and OpenAI have
no account here at all; Gemini has billing but shares the rate-limit trouble the
image client has. **No response from a real text model has ever been parsed** —
success parsing, refusals and token counts are mock-only for all three.

#### Harness trap: `import -window <id>` can wedge the whole X server

`import -window <someid>` on a window that has since been destroyed falls back
to *interactive* window selection, which grabs the X server and never returns.
Every other X client on that display then blocks, which looks exactly like the
application having hung. It cost a confused detour here. On the Xvfb harness use
`import -window root`, or better `xwd -root -silent`, and never a specific
window id.

Unit tests: `scribus/tests/inpainttests.cpp`, 28 cases. Contract (empty mask
bit-identical, fully masked, masks on every border and corner, cancel and
progress on both paths, alpha, ROI timing); which method Auto picks for thin,
flat and textured masks; and quality - texture energy survives, sharp vertical
and diagonal edges are not blurred away, coloured objects do not bleed, a
repeated pattern comes back in phase, text-like structure does not go grey,
discs on texture leave the background clean, and the crowd-like scene keeps its
ground texture and its horizon line.

Set `SCRIBUS_INPAINT_TEST_OUT=<dir>` to have the crowd-scene test write its
before, after and truth images there to look at; the automated run does not
depend on it.


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

### Crash capture in the package — `crashcapture/`

Everything crash capture needs now ships in the `.deb`, so a fresh machine gets
it from `dpkg -i` instead of by hand: the `scribus-debug` gdb launcher, its
"Scribus (Crash Capture)" desktop entry, `housekeeping.sh` in
`libexec/scribus-debug/`, and the `scribus-housekeeping` systemd **user** units.
`postinst` enables the timer with `systemctl --global` (so it covers users
created later too), creates `~/scribus-crashlogs` for the installing user, and
points the `.sla` association at `scribus-debug`. `postrm` reverses all of that
and **deliberately leaves `~/scribus-crashlogs` alone** — an uninstall must not
destroy the backtraces the machine was instrumented to collect.

The normal `scribus` launcher is untouched: crash capture is a separate command
and a separate desktop entry.

Traps:

- **Every `cpack -G DEB` package built before this was unusable.** The DEB
  generator stages into **`/usr`** by default, ignoring `CMAKE_INSTALL_PREFIX`,
  but `/usr/local/lib/scribus/plugins/` and `/usr/local/share/scribus/` are
  **compiled into the binary** (`strings scribus.bin | grep /usr/local`). So the
  package put the format loaders at `/usr/lib/scribus/plugins/`, where the
  binary never looks: no import/export plugins, and documents that will not open
  at all. `CPACK_PACKAGING_INSTALL_PREFIX` is now pinned to
  `CMAKE_INSTALL_PREFIX`. Symptom to recognise: a `.deb` that installs cleanly
  and then behaves like a Scribus with no file formats.
  Note this did **not** affect the `checkinstall`-made package on the dev
  machine — that one captured a real `make install` and so has the right prefix.
- **`~/.config/systemd/user` outranks the packaged unit.** A machine that had
  the hand-made per-user units installed keeps using them, including a stale
  `ExecStart` pointing at a script that has since moved. Remove the per-user
  copies to let the packaged one take over.
  This recurred on the dev machine on 2026-08-23: the stale per-user unit
  failed `203/EXEC` daily while the packaged one worked, so whether
  housekeeping ran depended on which user manager picked it up. Fixed by
  deleting the per-user `.service`/`.timer`, not by editing them.
- The unit uses `%h`, not a baked-in home, because one packaged unit serves
  every user. `ConditionPathExists=%h/scribus-crashlogs` keeps it a silent
  no-op for users who never run `scribus-debug`.
- `scribus-debug` runs the binary through a `libexec/scribus-debug/scribus`
  symlink so argv[0] keeps the prefs profile at `~/.config/scribus`; gdb passes
  the path it is given as argv[0], so `exec -a` is not available here.
- `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` was tried and **left off**: it pushes
  packaging past 10 minutes on this tree. The `.deb` therefore declares no
  library dependencies — the target machine needs the Qt6 runtime already.

#### The in-process crash handler — `main_nix.cpp`

`scribus-debug` only captures a crash for sessions launched through it. Every
other session goes through `initCrashHandler()`, and until 2026-08-23 that path
recorded **nothing at all**: `defaultCrashHandler()` wrote no log, raised a
modal `ScMessageBox` from signal context, then called `exit(255)`.

Three measured consequences, each of which hid real crashes:

- **`exit(255)` is a clean exit, so the kernel writes no core.** A crash the
  handler processed successfully left no trace anywhere — not in
  `~/scribus-crashlogs`, not in `coredumpctl`, not in the journal. Verified by
  dismissing the dialog on a test instance: coredump count went 35 → 35.
- **The modal dialog blocks forever.** `ScMessageBox::critical` runs a nested Qt
  event loop from a signal handler; with nobody to click OK the process sits in
  `QDialog::exec()` indefinitely. The operator force-quits, SIGKILL cannot be
  caught, and the crash is invisible. Confirmed by live backtrace:
  `defaultCrashHandler` → `QDialog::exec` → `ppoll`, still alive after 20 s.
- **The only crashes that ever produced evidence were the ones where the handler
  itself crashed.** Every stored `scribus.bin` core reads
  `SigCgt=0x00000001000040ab` — SIGILL/SIGABRT/SIGFPE caught, SIGSEGV *not* —
  which is the kernel forcing `SIG_DFL` after a second fault inside the handler,
  where SIGSEGV was blocked. A healthy process reads `…44ab`. That is why the
  Aug 16–17 cores all share one `~QMdiSubWindow → QFontCache` signature: it is
  the handler's own teardown faulting, not the original bug.

The handler now writes evidence first and gets out of the way:
`alarm(30)` → async-safe log → `notify-send` → emergency save → re-raise
`SIG_DFL`. What matters if you touch it again:

- **The order is the whole design.** The log reaches disk *before*
  `emergencySave()` runs, because `emergencySave()` is Qt code in signal context
  and is the call most likely to fault. Do not move it earlier.
- **`alarm()` must be armed first.** The old code armed `alarm(300)` *after* the
  blocking dialog, where it could never bound the hang it existed to bound.
- **Async-safety is not optional on the logging path.** No `malloc`, no
  `printf`, no Qt: raw `write(2)` helpers plus `backtrace_symbols_fd`, the
  non-allocating variant. `printBacktrace()` in `util_debug.cpp` uses `new`,
  `backtrace_symbols` and `__cxa_demangle` — never call it from a handler.
  `backtrace()` is primed once at init because its first call may `dlopen`.
- **`SA_ONSTACK` + `sigaltstack` is what makes a stack-overflow SIGSEGV
  catchable at all** — without an alternate stack the kernel cannot push a
  signal frame and kills the process outright.
- **Re-raising instead of exiting costs a ~15 MB core per crash** via
  systemd-coredump, and the exit status becomes signal death (139), not 255.
- SIGBUS was missing from the installed set and is now included.

Names in the log are mangled on purpose (demangling allocates); run
`c++filt < crash-*.log`.

Not exercised: the `notify-send` branch (`notify-send` is not installed on the
dev machine, so `asNotify()` is a no-op there and has never run), the
nested-fault path, and the stack-overflow case.

---

### Kasm workspace image — `kasm/`

Browser-streamed Scribus for demos and (eventually) cloud-hosted production:
`kasmweb/core-debian-trixie` + KasmVNC + XFCE, Scribus built from source into
`/usr/local`. Built with `docker build -f kasm/Dockerfile.kasm .` from the
repository root.

**The base image is Debian trixie on purpose.** Every dependency then resolves
to the *same version the development host builds against* — verified inside the
image: `qt6-base-dev 6.8.2+dfsg-9+deb13u2`, `libonnxruntime-dev 1.21.0+dfsg-1`,
`libpoppler-cpp-dev 25.03.0-5+deb13u4`. An earlier draft targeted
`core-ubuntu-noble`, which ships **Qt 6.4.2** and packages **no ONNX Runtime at
all**; `QT_MIN_VERSION` is 6.4.0 so it would configure, but it meant compiling
this fork against a Qt it had never been built with plus hand-installing
Microsoft's ONNX tarball. That draft was never once built.

Traps:

- **Build-time `HOME` must be `/home/kasm-default-profile`, not
  `/home/kasm-user`.** `/dockerstartup/kasm_default_profile.sh` copies the
  former into `$HOME` at session start (gated on `$HOME/.bashrc` not existing).
  Anything baked straight into `/home/kasm-user` is destroyed the moment
  persistent profiles are enabled, because the user's volume mounts over that
  path — so it demos perfectly and fails in production. Reset `ENV HOME` back
  to `/home/kasm-user` before the final `USER 1000`.
- **`ml-deshabhimani.mim` and `te-praja-sr.mim` are not in any Debian package.**
  They are hand-installed on the host and not dpkg-owned, so installing
  `fcitx5-m17n` yields the stock m17n tables and *silently* substitutes a
  different keyboard for the one the newsroom types on. Both now live in
  `kasm/ime/` and are copied to `/usr/share/m17n/`. Re-copy them after any
  host-side update; nothing syncs them.
- **SAM models do not belong in the default profile.** `SamSegmenter::modelDir()`
  is hardcoded to `$HOME/.config/scribus/sam/` with no system fallback
  (`scimagesam.cpp:26`), so baking them in makes Kasm `cp -rp` 43 MB into every
  user's home on first launch. They live once at `/opt/scribus/sam` and
  `kasm-launch.sh` symlinks them in — which also lets an admin bind-mount a
  replacement set read-only without touching profiles.
- Wait for the desktop with Kasm's own `/usr/bin/desktop_ready`, not a
  hand-rolled `until pgrep -x xfce4-session`; the base image's DE can change.
- The `pgrep` guard must match **`scribus.bin`**, not `scribus` — the installed
  `/usr/local/bin/scribus` is a wrapper that `exec -a scribus`es the real ELF,
  so the running process never carries the wrapper's name. Guarding on the
  wrong name spawns duplicate windows on every retry.

---

## Desh RIP — standalone CTP screening tool

A **separate application, not part of this repository**: `/home/s1/desh-rip/`,
Python 3 + PyQt6. Takes the imposed PDF that this fork's Impose Pages feature
produces (Extras → Impose Pages → Preview PDF / Send to CTP, uncommitted work
on `feature/ctp-output`) and converts it to 4 separated 1-bit CMYK TIFF plates
via Ghostscript's `tiffsep1` device, with real AM (amplitude-modulated)
halftone screening — the working recipe is Key Trap #5 above; every mechanism
in the feature's original spec (`sethalftone`/`setpagedevice` alone) silently
produced wrong output, discovered only by testing pixel content, not by
whether the command exited 0.

**Stack**: PyQt6, installed via `apt install python3-pyqt6` (`python3-pyqt6`
6.9.0-2). Neither PyQt6 nor PySide6 was preinstalled on this machine, but both
are real, clean Debian packages here — no pip/venv workaround needed
(`python3-pyside6.qtwidgets` 6.8.2.1-4 is the PySide6 fallback, also available,
not installed).

**Structure**:

| File | Role |
|---|---|
| `desh_rip/profiles.py` | `Profile` dataclass, JSON persistence at `~/.config/desh-rip/profiles.json` |
| `desh_rip/ghostscript.py` | The actual `tiffsep1` invocation, implementing the Key Trap #5 recipe. Raises `ValueError` immediately for an FM profile — no silent wrong-screening output |
| `desh_rip/settings_dialog.py` | Print Profiles UI: profile list, AM/FM editor, per-channel angles, "standard newspaper angles" checkbox |
| `desh_rip/main_window.py` + `main.py` | App shell; "Generate Plates..." runs the real pipeline end to end |

**State: AM screening complete and verified working. FM screening
deliberately unimplemented** — blocked on Key Trap #5's still-unsolved FM
mechanism. The FM radio button is present in the UI (matches the originally-
specified mockup) but labelled "not yet supported"; selecting it and trying
to generate plates fails with a clear message rather than producing incorrect
output.

**Verification**: ran `ghostscript.generate_plates()` against a real
production imposed PDF and diffed the **raw pixel data** (`Image.tobytes()`
MD5, not just file bytes — those differ on every run due to a TIFF timestamp
tag) against the manually-validated reference command from the Key Trap #5
investigation — **100% pixel-identical on all 4 channels**. GUI launched and
screenshotted on an isolated Xvfb display (`:77`, never the operator's live
desktop) — main window and Print Profiles dialog both confirmed rendering
correctly; the AM/FM radio toggle confirmed switching the settings groups
correctly.

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

### 5. Ghostscript silently ignores every classic halftone operator — `sethalftone`/`setpagedevice`/`setscreen`/`setcolorscreen` all do nothing

Discovered while building CTP plate generation (imposition PDF → 4 separated
1-bit TIFFs via `gs -sDEVICE=tiffsep1`). Every "obvious" PostScript mechanism
for controlling AM screening frequency/angle gets **accepted** (confirmed via
`currenthalftone` immediately reflecting the request) but has **zero effect on
the rendered output**:

- `sethalftone` with a `HalftoneType 1` or `5` (multi-colorant) dict
- `setpagedevice` with the same dict
- `setscreen` (the classic Level 1 freq/angle/proc operator)
- `setcolorscreen` (the RGB+gray 12-argument form)

Verified via 15+ isolated tests, not just "looked the same": byte-identical
output across wildly different requested frequencies (20lpi vs 150lpi), on
two different devices (`tiffsep1` and `pbmraw`), on both PDF and PostScript
input (ruling out the newer C-based `pdfi` PDF interpreter as the cause —
plain `.ps` input was equally unaffected), with `-dPreserveHalftoneInfo=false`
/ `-dAccurateScreens=true` / `-dUseFastColor=true` all tried in combination
and making no difference. Confirmed on a trivial single-rectangle synthetic
file (`0.37 setgray; 0 0 200 200 rectfill`) to rule out any real-content
explanation (embedded pre-screened images, antialiasing artifacts, etc.).

**What actually works** — found in Ghostscript's docs, not in any PostScript
operator:

```
-dDITHERPPI=<lpi>   forces the requested frequency to actually be used. Without
                    it, gs silently substitutes its own "minimal sized periodic
                    screen" for colour accuracy, discarding the requested
                    Frequency entirely — confirmed by the internal string
                    `strings libgs.so.10` reveals: "No additional dithering,
                    creating minimal sized periodic screen".
-dCOLORSCREEN       required for per-channel Angle to differ AT ALL. Without
                    it every CMYK channel renders at the same angle regardless
                    of a HalftoneType 5 dict's per-colorant Angle — verified
                    visually: Cyan (15°) and Magenta (75°) were pixel-identical
                    until this flag was added, then visibly different.
```

Both are plain `-d` command-line switches, not PostScript. The halftone dict
itself is still required for the actual per-channel Frequency/Angle/SpotFunction
values — it just silently does nothing without these two flags present too.

Working recipe (verified pixel-for-pixel reproducible, including from Python
via `subprocess` — see Desh RIP below):

```
gs -dBATCH -dNOPAUSE -dSAFER -sDEVICE=tiffsep1 -r1200 -sCompression=g4 \
   -dDITHERPPI=150 -dCOLORSCREEN \
   -sOutputFile=out_%d.tif \
   -c "<< /HalftoneType 5
        /Default << /HalftoneType 1 /Frequency 150 /Angle 45 /SpotFunction {dup mul exch dup mul add 1 exch sub} >>
        /Cyan    << /HalftoneType 1 /Frequency 150 /Angle 15 /SpotFunction {dup mul exch dup mul add 1 exch sub} >>
        /Magenta << /HalftoneType 1 /Frequency 150 /Angle 75 /SpotFunction {dup mul exch dup mul add 1 exch sub} >>
        /Yellow  << /HalftoneType 1 /Frequency 150 /Angle  0 /SpotFunction {dup mul exch dup mul add 1 exch sub} >>
        /Black   << /HalftoneType 1 /Frequency 150 /Angle 45 /SpotFunction {dup mul exch dup mul add 1 exch sub} >>
       >> sethalftone" \
   -f input.pdf
```

Two more traps bundled into the same investigation:

- **`/SpotFunction /SimpleDot` (a name) is invalid** — PostScript's `sethalftone`
  requires a **procedure** (`{...}`). `SimpleDot` as a *named* spot function is
  a PDF-halftone-dictionary-spec convention (Table 100 in the PDF spec), not a
  PostScript one; using it throws `Error: /typecheck in --sethalftone--`.
- **Argument order**: `-sOutputFile` must come *before* `-c`/`-f`. `-f file.pdf`
  triggers immediate execution, so any switch after it (e.g. `-c "..." -f
  input.pdf -sOutputFile=...`) arrives too late for the device to see —
  `tiffsep1` then reports "requires an output file but no file was specified"
  even though one was given.
- `tiffsep1` names its own output
  `<template-with-%d-substituted>(<SeparationName>).tif` (e.g.
  `out_1(Cyan).tif`), never `<template><SeparationName>.tif` — trips up any
  code, or command typed by hand, that assumes the latter.

**FM/stochastic screening (`HalftoneType 3`, threshold array) is still
unsolved.** Tested with and without `-dDITHERPPI`; produced the same default
periodic pattern regardless of the threshold array's actual contents, every
time. Needs a fresh investigation pass — possibly also needs a properly-
constructed blue-noise matrix rather than the crude random-shuffle array used
to test, since a bad matrix could itself explain "no visible difference".

### Other traps that cost real time

- **Two live `QShortcut`s on one key fire neither.** Qt reports the key as
  ambiguous and delivers nothing to either. The Paragraph Styles panel owns
  three families (chains, column configs, per-style keys, all
  `Qt::ApplicationShortcut`), so a conflict "Replace" that only clears the other
  owner's *stored* key leaves its `QShortcut` alive until the next rebuild — the
  new key looks assigned, is saved, and is dead. `resolveShortcutConflict()`
  ends with `rebuildShortcuts()` for that reason; keep it there. Related: the
  Style Manager builds a `ScrAction` per style shortcut but never adds it to a
  widget, so those cannot fire and cannot cause this ambiguity — the panel's
  `QShortcut` is the only thing that applies a style key.
- **Open the undo transaction before `sizeItem()`, not after.** `applyColumnConfig()`
  began its transaction after `setColumns`/`sizeItem`, so the frame resize was
  its own undo step and Ctrl+Z reverted the paragraph styles but left the new
  width and column count. Every doc call that records undo (`sizeItem`,
  `createPageItem`, `autoFitFrameHeight`) has to sit inside the transaction.
- **Column config ↔ Design Style can call each other.** A Design Style has a
  "columns" config index; a config now has a Design Style name. `applyColumnConfig()`
  applies the design with its columns step skipped, and `m_applyingDesignStyle` /
  `m_inColumnConfig` guard the other direction; remove either guard and Ctrl+Alt+N
  recurses.
- **Design/Column Style paragraph styles are applied by attributes, not by name**
  (pre-existing, not changed). `itemText.applyStyle(pos, namedStyle)` copies the
  named style's attributes; `ParagraphStyle::applyStyle` only sets the parent when
  the *source* has one, and a document style has none. The text looks styled but
  the paragraph's parent stays what it was, so later edits to the style do not
  propagate and the .sla records the old parent. Scribus's own path
  (`itemSelection_SetNamedParagraphStyle`) applies a style whose *parent* is the
  name. Two styles with identical attributes make this invisible in a test.
- **`QKeySequenceEdit` records the Enter that confirms it.** The editor keeps
  focus for ~1 s after a key; an Enter inside that window becomes the recorded
  shortcut. The Assign Shortcut dialog moves focus to OK on `editingFinished`
  and refuses a bare Return/Enter/Escape as a result.

- **A placed image is drawn by four renderers, and three of them reload it from
  `Pfile` rather than using `pixm`.** `PageItem_ImageFrame::DrawObj_Item` ends
  in one `p->drawImage()`, which makes it look like the single place to hook
  anything image-related. It is not: `pslib.cpp` and `pdflib_core.cpp` call
  `PS_image`/`PDF_Image` with `item->Pfile`, and `scpageoutput.cpp` calls
  `loadPicture` again. Anything hooked only into the canvas draw path is
  **invisible in the PDF and on paper, silently**. The one thing all four share
  is `ScImage::applyEffect` on `item->effectsInUse` — which is why the eraser
  mask is carried there.
- **In a CMYK `ScImage`, `qAlpha()` is the black plate.** See
  `writePSImageToFilter` (`scimage.cpp:2580`): `k = qAlpha(r)`. Any "just set
  the alpha channel" idea applied on an export path punches holes in the K
  separation instead of making pixels transparent. Transparency on those paths
  goes through the separate mask array / `SMask`, never through the samples.
- **`getImageEffectsModifier()` concatenates every `effectParameters` verbatim
  into the image cache key.** Fine for `"0.5 1.0"`; ruinous for an effect whose
  parameters are a base64 PNG rebuilt on every brush stroke. Anything bulky
  stored in an effect must contribute a digest, not its payload.
- **`EffectsDialog::saveValues()` clears the effect list and rebuilds it from
  the visible widget**, so any effect code the dialog does not know about is
  silently dropped the moment the user presses OK. An erased frame lost its mask
  to a dialog the user only opened to look at. Unknown effects now have to be
  carried across explicitly (`m_eraserMaskParams`).
- **Brush dabs must be spaced along the whole stroke, not per mouse-move
  event.** Two separate versions of this bug: compositing each event's coverage
  into the mask cumulatively let overlapping feathers stack up and left one
  visible scallop per event; and restarting the dab phase at each event made the
  same gesture come out differently depending on the event rate. Fix is a
  coverage buffer accumulated with `max()` over the whole stroke, applied
  against the stroke-start mask each time, plus a carried dab-distance. Both are
  pinned by the regression checks in the eraser mask test.
- **`print $_siginfo` does not error on a clean exit under gdb 16.3.** The
  original `scribus-debug` chained crash-only `-ex` commands after it and relied
  on that error to stop batch gdb from reaching them. gdb 16.3 prints
  `$1 = void` and carries on, so **every ordinary session** wrote the CRASH
  banner, was renamed to `crash-<ts>.log`, kept forever, and popped a "Scribus
  crashed" notification on quit — burying the real crash in noise. Use a gdb
  command file guarded by `if $_isvoid($_exitcode)`; that is void only when a
  signal killed the process. Verified both ways against gdb 16.3.
- **Plugin ABI** — plugins load from `/usr/local/lib/scribus/plugins/` even when
  running the build-dir binary. Change a shared struct, rebuild only the main
  binary, and you get SIGSEGV on document load. Full build **and**
  `cmake --install`.
- **`docker build` has no DNS on this laptop, and apt reports it as missing
  packages.** `docker run` resolves fine; BuildKit's build containers cannot
  reach the LAN resolver while strongSwan/IPsec is active. apt treats the failed
  refresh as a *warning* ("Some index files failed to download … old ones used
  instead"), carries on with an empty list, and then prints `Unable to locate
  package` for **every** dependency — including `libssl-dev` and
  `libonnxruntime-dev`, which obviously exist. It reads exactly like a wrong
  package list in the Dockerfile and cost a full four-minute apt stage to
  misdiagnose. Build with `--network=host`; `kasm/test-local.sh` probes and adds
  it automatically.
- **A piped `docker build … | tail` reports `tail`'s exit status, not the
  build's.** A failed build looked like a clean `exit code 0` with an empty log.
  Redirect to a file and check `$?`, or read `PIPESTATUS[0]`.
- **Docker's data root here is `/home/docker`** (`/etc/docker/daemon.json`), so
  `df /` is the wrong filesystem to check before a large build — `/` had 10 GB
  free while the builder had 60 GB.
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


## Linked / embedded images (uncommitted as of 2026-10-02, branch feature/ctp-output)

Code: `scribus/suneerimagelinks.{h,cpp}` (one definition of linked / embedded / missing, embed with
undo), `ui/suneerlinkedimagesdialog.*`, badge in `PageItem::DrawObj_Decoration`, check in
`ScribusMainWindow::suneerLinkedImagesCheck`, preflight `PreflightError::LinkedImage`.

Traps:

- **"Embedded" = `isInlineImage` + `isTempFile`, and `Pfile` is then a temp copy** (`/tmp/scribus_temp_*`).
  Anything that derives a path from `Pfile` (caption sidecars, `derivedImagePath`) now sees the temp
  path. Ctrl+I therefore embeds only AFTER the 200 ms caption update in `canvasmode_imageimport.cpp`.
- **`ScribusDoc::loadPict(reload=false)` deletes `Pfile` when `isTempFile` is set.** A tool that does
  `item->Pfile = output; loadPict(output, ...)` on an embedded frame deletes its own output. Clear
  `isTempFile` first, embed again after (Remove Background, Crop/Resize, Edge Feather do this now).
- **`~PageItem` deletes `Pfile` when `isTempFile` is set.** `restoreGetImage` now clears the flags
  when it relinks to a file that is not a `scribus_temp_` file, so undo cannot delete a user's picture.
- **`loadPict` is NOT hooked centrally**: paste and the importers go through it with `isLoading()`
  false, so a central hook would silently embed pasted frames. `embedPlaced()` is called at the user
  placement sites instead; a new placement path must call it too.
- The per-document "don't ask" flag is DOCUMENT attribute `SuneerLinkedImagesNoAsk`, written only
  when set. `CheckerPrefs::checkLinkedImages` defaults to true in the constructor because the older
  format loaders never set it.
- A dead NFS mount (`/mnt/F`) used to hang every file dialog (volume listing) and startup. Fixed by
  `NetPathGuard` — see "Dead network folder" below.


## PDF export presets and Default (2026-10-02)

Code: `scribus/pdfpresets.{h,cpp}` (store, JSON, Default), `ui/pdfexportdialog.*` (preset row, apply,
"(modified)"), `ScribusMainWindow::doSaveAsPDF` + `suneerSaveAsPDFDefault/WithPreset` (direct export,
submenu), `resources/pdf-presets/` + `tools/update-office-pdf-presets.py` + `tools/release.sh` step 3b2.

- **`TabPDFOptions::restoreDefaults()` ignores its `Optionen` argument** and reads the tab's own
  `Opts` reference — which in the export dialog IS `doc->pdfOptions()`. So applying a preset has to
  write the preset into the document's options and then call `restoreDefaults()`. That is why
  `doSaveAsPDF` keeps a copy (`DocPdfOptionsKeeper`) and puts it back on every return path; the whole
  export code reads `doc->pdfOptions()` too.
- **The document's own settings change only when `PDFExportDialog::keepsDocumentSettings()` is false**:
  no preset was ever applied in that dialog, or "Use this document's own saved settings instead" is
  ticked. Otherwise only `fileName` survives the export.
- **`PDFOptionsIO` is not used**: it predates half the options (marks, viewer, outline list, doc
  bleeds...). Presets are JSON written by `PdfPresets::toJson`; a new `PDFOptions` field must be added
  to `toJson`/`fromJson` or it is silently not part of a preset.
- **Font lists are rebuilt per document** (`applyPreset`): all fonts go to EmbedList (or SubsetList when
  the preset says "subset"); the tab then moves the ones that cannot be embedded whole. The baseline for
  "(modified)" is taken from the widgets AFTER applying, not from the file, because of that.
- **"(modified)" is a 400 ms poll** comparing `PdfPresets::fingerprint()` of the widget state; no
  per-widget wiring. `TabPDFOptions::presetOverridden()` is now ignored.
- **CMS is switched on at export, not on apply** (`m_enableCmsOnExport`): the old built-in presets
  called `m_doc->enableCMS(true)` on selection; with a Default applied on every open that would change
  a document just by opening the dialog. `collectOptions()` uses `cmsAvailableForExport()`.
- **The Default is a file** (`pdf-presets/default.txt`), not a prefs key: prefs are written on exit and a
  crash would lose the choice.
- **The direct-export preset name and the submenu actions are file-statics in scribus.cpp**, not
  members (plugin ABI). `SaveAsPDF()` clears the static first: a direct export parked at the Preflight
  Verifier and then abandoned must not hijack the next normal Save as PDF.
- **Two QMenu objects exist for every submenu** (File > Export, Open Recent...): one is a stale twin
  outside the menu bar. A test must walk `menuBar()`, not `findChildren<QMenu*>()`.
- **PyQt aborts the process on an exception inside a slot** — a harness bug looks like a Scribus crash
  (signal 6, crash log, emergency file). And a `QFileDialog::get*` static is a native GTK dialog here,
  invisible to PyQt; Export/Import use `CustomFDialog` (also NetPathGuard-safe).
- Office presets: `install(DIRECTORY ...)` so files added after configure are packaged; the copy script
  blanks passwords because `resources/` is in a public repo.
- Harness: `~/scribus-crashlogs/pdfpresettest/run.sh <label> <s1|s2|s3|s4>.py old.sla`.


## Align and Distribute button on the control bar (2026-10-02)

`SuneerControlBar` is a QToolBar holding ONE container widget (all the rows) plus trailing actions.
The button is a trailing QAction after a stretching spacer widget, so it is pinned to the right edge
and no `show*Widgets()` pass can hide it. Code: `onAlignDistributeClicked()`, `updateAlignDistributeButton()`,
`placeAlignPaletteByButton()` in `ui/suneercontrolbar.cpp`.

- **It is its own QAction, not `scrActions["toolsAlignDistribute"]`.** It is hidden with no selection
  (`QAction::setVisible`; first version disabled it, the operator preferred an empty bar), and hiding
  or disabling the shared action would take the Windows-menu entry with it. The palette itself is
  never touched when the selection empties.
- **The Windows-menu action is not checkable** (ADS "show" mode: `setToggleViewAction` with a
  non-checkable action only ever opens/raises). So "is it open" is read from the dock itself
  (`isClosed()`, `isTabbed()`, `isCurrentTab()`), and the button follows `viewToggled` /
  `visibilityChanged`, which also covers opening it from the menu or closing it with its own X.
- **The palette does not exist when the control bar is constructed** (toolbars are built before the
  docks), so the signal connection is made lazily in `updateAlignDistributeButton()`.
- **The bar's ">>" is in Qt's expand mode, not menu mode**: clicking it grows the bar by a row instead
  of popping up a menu, and the button shows in that row. With one very wide widget action in the bar
  that is what QToolBarLayout does; there is no ">>" menu to add an entry to.
- The floating palette is moved only when the button OPENS it (was closed), one event-loop turn after
  `toggleView(true)`, because ADS restores the old floating geometry inside that call.
- Harness: `~/scribus-crashlogs/aligntest/run.sh a` (selection types, toggle, floating, narrow) and
  `AT_SCRIPT=t2.py run2.sh b` (menu sync, Reference retention, ">>").


## Linked / missing image badges: fixed screen size, label, size preference (2026-10-02)

`SuneerImageLinks::badgeImage()` paints the badge once per (status, size, with/without label) into a
cached QImage; `PageItem::DrawObj_Decoration` draws it through a `1/zoom` scale, so one image pixel is
one screen pixel. Size pref: prefs context `suneer_images`, key `badge_size` (0/1/2 = 20/24/28 px,
default 2), combo in Preferences > Item Tools.

- **The image must be `Format_ARGB32`, not premultiplied.** `ScPainter::drawImage(QImage*)` paints the
  colour channels as RGB24 and uses the same buffer's alpha as a mask; premultiplied data darkens every
  antialiased edge.
- **`slotPrefsOrg` only repaints the canvas when the page shadow changed.** The badge size is not in
  `ApplicationPrefs`, so the old value is read before the dialog and compared after; without that a new
  size showed only at the next unrelated repaint.
- **Status is live.** A frame whose file is deleted after loading still shows LINK until the document is
  reopened (`statusOf` goes by the loaded state); recreate the file and a MISSING frame turns to LINK
  on reopen. The test document has to be reopened from disk to get a MISSING badge.
- The 8 px inset is tied to `drawSelectionHandle()` in util_gui.cpp (6 px squares centred on the frame
  edge). Change one, check the other.
- Harness: `~/scribus-crashlogs/badgetest/run.sh <label> <0|1|2|default>`, `measure.py` for pixel sizes.


## Dead network folder never hangs startup or the file dialog (2026-10-02)

Code: `scribus/netpathguard.{h,cpp}`. A stat() on an NFS "hard" mount whose server is gone never
returns and cannot be given a timeout, so the stat runs in a detached helper thread and the GUI
thread waits at most 2 s (`NetPathGuard::reachable`). No answer -> the folder is "skipped": every
path under it is refused at once, and `ScribusMainWindow::showNetworkPathNotes()` puts one note in
the status bar. The stuck thread is the recovery detector: when the server returns, its stat()
returns and the folder stops being skipped.

Guarded: extra font dirs, user ICC dir, recent scrapbooks, recent documents, template dir + Template
Style Source (lock sweep, New from Template, Paragraph Styles panel), autosave dir + documents dir
(crash-recovery search, autosave timer, emergency save), and `ScFileWidget`/`CustomFDialog` (start
folder, QtProject.conf sidebar/history/lastVisited, /media volumes).

Traps:

- **The splash text lies about where a hang is.** "Reading Scrapbook" is only the last
  `setSplashStatus()` call; the real hang was 60 lines later, in the template-lock sweep
  (`DocumentLock::removeOwnDeadLocksIn(templateSearchPaths())`), with Templates on the share.
- **`reachable()` means "answered", not "exists".** Callers keep their own `exists()` test.
- **Startup checks are started early and waited on late** (`ScribusCore::precheckNetworkPaths`), so the
  2 s runs alongside font/plugin init. A new startup path should be added there, or it costs its
  own 2 s.
- **The dead folder is found by walking the path top-down in the helper thread**; the component it is
  stuck on is the mount point or the symlink into it (`~/Desktop/F` -> `/mnt/F`). A symlink into an
  already-skipped folder is skipped without a second wait.
- **QFileDialog stats its saved places inside its own constructor** (QtProject.conf `shortcuts`,
  `history`, `lastVisited`, plus a process-wide "last visited" and the cwd). `ScFileWidget` therefore
  filters the settings BEFORE the base constructor runs (`hideUnreachablePlaces()` in the base
  initialiser) and puts the hidden entries back on `destroyed`. Its destructor always leaves the
  dialog on the home folder so the process-wide value is never a share.
- **`QStorageInfo::mountedVolumes()` statfs()es every mount** — that was the "volume listing" hang.
  /media mount points are now read from `/proc/self/mountinfo`.
- **`PrefsManager::documentDir()` returns the local documents folder while the configured one is
  skipped.** `appPrefs.pathPrefs.documents` itself is untouched and is saved back unchanged.
- **Recent scrapbooks are no longer stat'ed in `readPrefs`**; `initScrapbook()` decides (reachable and
  exists -> open; unreachable -> kept in prefs, not opened; gone -> dropped). The skipped list is a
  file-static in scribus.cpp, NOT a member: adding a member to `ScribusMainWindow` changes the layout
  the plugins are compiled against.
- **Not covered:** a share that dies in mid-session while a document on it is open (Save, the
  FileWatcher poll and linked-image reloads still block), fontconfig's own directories, and native
  (non-`CustomFDialog`) `QFileDialog::get*` calls.
- Test harness: `~/scribus-crashlogs/nfshang/run.sh <label> <hang|missing>`. `hangfs.py` is a FUSE
  filesystem that answers FUSE_INIT and nothing else; it is mounted on `/mnt/F` **only inside
  `unshare -m`**, never in the real namespace (it would freeze the desktop). Tested against that
  fake, not against a real dead NFS server.
