# Suneer Custom Scribus 1.7.3 — Changes & Features

## Build Info
- Base: Scribus 1.7.3
- Qt: Qt6
- Developer: Suneer. A (alp.suneer@gmail.com)
- Category: Newspaper Page Layout

---

## Custom Features Added

### 1. News Browser Panel
- Workflow API-ൽ നിന്ന് news fetch ചെയ്യുന്നു
- Edition, Page selection
- Multi-select with checkboxes + Select All
- Publish date default = next day
- Single line display + mouse tooltip
- Modal dialog on click — full news preview
- Place Selected button
- Image auto-place with text wrap
- SR Menu Preferences-ൽ enable/disable option

### 2. Suneer Control Bar
- Font family, size, style controls
- Text alignment, paragraph spacing
- Image fit/flip/rotate controls
- Background Remove (AI — rembg)
- Crop + Resize tool (Photoshop-style)
- Contour draw + edit
- Text wrap offset +/- buttons

**Keyboard navigation** — Ctrl+Shift+F (Focus Font Family) കൊണ്ട് toolbar-ലെ
font field-ലേക്ക് focus പോകും. അവിടെ നിന്ന്:
- **Tab / Shift+Tab** — toolbar-ലെ അടുത്ത/മുൻപത്തെ control-ലേക്ക് നീങ്ങും.
  ആദ്യത്തെ നാലെണ്ണം സ്ഥിരമായി ഈ ക്രമത്തിൽ (styling-ന് പതിവായി ഉപയോഗിക്കുന്നവ):
  **Font Family → Font Style → Font Size → Line Spacing**.
  ബാക്കിയുള്ളവ toolbar-ന്റെ വായനാക്രമത്തിൽ — row 1: style effects, alignment,
  columns, column gap; row 2: line spacing mode, tracking, baseline offset,
  horizontal/vertical scale, first line indent, space above/below.
  (17 control-കൾക്ക് ശേഷം Tab അമർത്തിയാൽ line controls-ലേക്ക് പോകും)
- **Enter** — value apply ചെയ്ത് caret തിരികെ text frame-ൽ, ഉണ്ടായിരുന്ന
  സ്ഥാനത്തുതന്നെ
- **Esc** — മാറ്റം വേണ്ട, caret തിരികെ text frame-ൽ
- Text frame-ൽ caret ഉള്ളപ്പോൾ **Tab** പഴയപടി tab character തന്നെ ഇടും
  (ഇതിന് മാറ്റമില്ല)

### 3. Crop Tool (Photoshop-style)
- CR button → crop mode activate
- Drag to select crop area
- Dark overlay + rule of thirds grid
- White handles at corners/edges
- **Enter** → crop apply
- **Escape** → cancel
- Checkbox: Enable fixed W/H resize
- Normal crop → file save ആകും
- Fixed size crop → exact mm output

### 4. Image Features
- Background Remove (rembg AI)
- Fit Frame to Image / Fit Image to Frame
- Image flip H/V
- Image rotation
- Auto contour from image clip

### 5. Malayalam/Telugu/Hindi Input
- fcitx5 + m17n support
- Canvas widget IME fix (Qt6)
- Direct Unicode insertion patch
- Ctrl+Space → language switch

### 6. Text Features
- Ctrl+. → Text size enlarge
- Ctrl+, → Text size reduce
- Auto fit frame height
- Telugu/Malayalam direct typing in text frame
- Paragraph Styles panel: Next Style chain ഉള്ള row-കളിൽ chain-link icon
  (പഴയ blue box glyph-നു പകരം) + "Next style: ..." tooltip; icon-ൽ hover
  ചെയ്യുമ്പോൾ hand cursor, click area വലുതാക്കി (full row height)

### 7. Faircode Frames (Text Frame Style Presets)
- Scrapbook palette-ൽ built-in "Faircode Frames" tab (read-only, auto-open)
- 23 ready-made decorative frame templates:
  - QuoteA/QuoteB (corner L-lines + quote glyphs, orange/green & pink/cyan)
  - TitleBox, AngledBar, Ribbon (red banner, notched ends), TitleRule, Highlight
  - CalloutL/CalloutR (leader line + dot), Badge (circle label)
  - Brackets, TintedBox, SideRule, PullQuote, NumberBox (listicle badge)
  - ShadowBox, DoubleBox, DashedBox
  - Wave 3 (Hindi newspaper reference designs): AttrQuote (yellow-tint
    attributed quote — author + designation lines), SectionStrip (yellow strip
    + red/black bars), NewsBox (bordered item box, corner accents,
    kicker/title/body), BannerHead (rounded blue banner + red rules),
    LedeBars (fading blue bars + bold lede line)
- വലിയ white-background thumbnails (96px) + മുഴുവൻ പേരും കാണാം
- Double-click thumbnail → page-ൽ insert; drag → cursor position-ൽ
- എല്ലാ template text-ഉം Malayalam-ready (Noto Sans Malayalam styles: FC Body ML / FC Quote ML / FC Title ML / FC TitleDark ML / FC BodyBold ML)
- FC color palette (FC-Red/Orange/Green/Pink/Cyan/Yellow/Primary) insert ചെയ്യുമ്പോൾ document-ൽ ചേരും — Edit > Colours-ൽ മാറ്റാം
- Group-ൽ double-click → text frame നേരിട്ട് edit mode (InDesign-style); decoration lines double-click → select
- Templates: `resources/scrapbook/faircode-frames/` (+ authoring scripts in `resources/scrapbook/authoring/`)

### 8. Newspaper PDF Export
- പുതിയ document-കൾക്ക് default: Lossy JPEG / High quality / Max Image
  Resolution 240 dpi — export size ചെറുതാകും, ഒന്നും set ചെയ്യേണ്ട
- PDF Export dialog-ൽ "News_Paper" preset — ഒറ്റ click-ൽ full press settings:
  - Lossy JPEG High + 240 dpi image cap
  - Output for Printer, CMYK conversion (Rel. Colorimetric / Perceptual)
  - Newsprint ICC profile: IFRA26S 2004 Newsprint (ISOnewspaper26v4
    install ചെയ്താൽ അത് automatic ആയി prefer ചെയ്യും —
    `/usr/share/color/icc/`-ൽ .icc ഇടുക)
  - PDF/X-1a:2001 + output intent embedded
  - Document color management off ആണെങ്കിൽ automatic ആയി on ആകും
- ഏതെങ്കിലും field മാറ്റിയാൽ preset combo "Custom" ആകും; per-export
  മാറ്റങ്ങൾ document-ൽ സൂക്ഷിക്കും

### 9. Image Bullets (Bulleted Lists)
- Style Manager → Lists & Drop Caps → Bulleted List-ൽ "Use Image" option:
  ഒരു PNG അല്ലെങ്കിൽ SVG image browse ചെയ്ത് bullet ആയി ഉപയോഗിക്കാം
  (arrows, logos, section markers...)
- SVG bullets ഏത് size-ലും sharp ആയിരിക്കും (auto high-res rasterization);
  .sla-യിൽ SVG source തന്നെ embed ആകും
- Size control: Auto (0.8em) / Scale % (auto size-ന്റെ 10–500%) / Fixed (mm) —
  വലുതാക്കാൻ "150%" എന്ന് കൊടുത്താൽ മതി; max 3em clamp
- V-Offset spinbox (mm, ±20): icon താഴ്ത്താൻ positive, ഉയർത്താൻ negative —
  headline-ന്റെ നടുവിൽ കൃത്യമായി വയ്ക്കാം
- Default placement മെച്ചപ്പെടുത്തി: icon ഇപ്പോൾ x-height-ന്റെ നടുവിൽ center
  ആകും (baseline-ൽ ഇരിക്കുന്നതിനു പകരം) — Malayalam headline-കളിൽ high
  ആയി float ചെയ്യില്ല
- Distance field പഴയതുപോലെ പ്രവർത്തിക്കും
- Canvas, PDF, Print, SVG, XPS — എല്ലാ export-ലും ഒരേപോലെ render ആകും;
  News_Paper preset-ന്റെ CMYK conversion automatic
- Image file .sla-യിൽ base64 ആയി embed ആകും — വേറെ PC-യിൽ തുറന്നാലും
  bullet കാണാം (file missing ആയാൽ auto-restore)
- Character bullets പഴയതുപോലെ; image unusable ആയാൽ character bullet-ലേക്ക്
  fallback

### 10. Paragraph Shading (InDesign-style)
- Style Manager → പുതിയ "Paragraph Shading" tab: paragraph-ന്റെ പിന്നിൽ
  background color band — text reflow ആകുമ്പോൾ band കൂടെ വളരും/നീങ്ങും
- Controls: On, Color + Tint %, Width (Column/Text), 4 padding
  (negative ആകാം), Corner Radius, Merge Adjacent
- Merge Adjacent: ഒരേ shading ഉള്ള അടുത്തടുത്ത paragraphs ഒറ്റ seamless
  block ആയി render ആകും (ഇടയിൽ seam ഇല്ല)
- Frame fill None ആയാലും paragraph-കൾക്ക് മാത്രം color കൊടുക്കാം;
  filled frame-ൽ ഒരു paragraph White/Paper കൊടുത്ത് mask ചെയ്യാം
- Canvas, PDF, Print, PS, SVG, XPS — എല്ലാ export-ലും ഒരേപോലെ;
  Paragraph Rules-നൊപ്പം ഒരേ style-ൽ ഉപയോഗിക്കാം (shade താഴെ, rule മുകളിൽ)
- എല്ലാ .sla format-ലും save ആകും; copy/paste, scrapbook-ലും travel ചെയ്യും

**Control Bar popup** — Style Manager തുറക്കാതെ, control bar-ലെ ▤ button
click ചെയ്ത് select ചെയ്ത paragraph-ന് നേരിട്ട് shading കൊടുക്കാം:
- Paragraph style-നെ ഒരിക്കലും edit ചെയ്യില്ല — override ആ paragraph-ന്
  മാത്രം. ഒരേ style ഉപയോഗിക്കുന്ന വേറെ paragraph-കൾക്ക് മാറ്റമില്ല
- Popup തുറക്കുമ്പോൾ ആ paragraph-ന്റെ ഇപ്പോഴത്തെ shading കാണിക്കും —
  style-ൽ നിന്ന് വന്നതായാലും നേരത്തെ override ചെയ്തതായാലും
- മാറ്റുന്ന value മാത്രം override ആകും; ബാക്കിയുള്ളവ style-നെ follow
  ചെയ്തുകൊണ്ടിരിക്കും (style പിന്നീട് മാറ്റിയാൽ അത് വരും)
- വ്യത്യസ്ത style-കളുള്ള paragraph-കൾ ഒരുമിച്ച് select ചെയ്ത് colour
  മാത്രം മാറ്റിയാൽ, ഓരോ paragraph-ലും colour മാത്രമേ override ആകൂ —
  padding, width, corner radius എല്ലാം അതത് paragraph-ന്റെ സ്വന്തം
  style-നെ follow ചെയ്യും
- "Reset to style" — override കളഞ്ഞ് style പറയുന്നതിലേക്ക് തിരികെ
- മുഴുവൻ popup interaction-നും ഒറ്റ Ctrl+Z
- Multi-paragraph selection-ൽ വർക്ക് ചെയ്യും
- Panel-ൽ Style Manager-ലെ അതേ controls തന്നെ (ഒരേ widget reuse ചെയ്യുന്നു)

### 11. Image Resize / DPI Tools
- Toolbar-ൽ "RS" button → **Resize Image** dialog: ഇപ്പോഴത്തെ pixel size,
  file size, frame size-ലെ effective DPI കാണിക്കും
- "Fit to frame @ N dpi" (default 240 — News_Paper standard; 300 preset):
  frame-ന് വേണ്ട pixel size automatic ആയി കണക്കാക്കും
- പുതിയ file `name_resized.jpg` ആയി source-ന്റെ അടുത്ത് save ആകും —
  **original file മാറ്റില്ല**; frame relink ആകും, page-ൽ ഒരേപോലെ കാണും
- Ctrl+Z → ഒറ്റ step-ൽ പഴയ image-ഉം scale-ഉം തിരികെ വരും
- Toolbar-ൽ **DPI field**: current effective DPI live കാണിക്കും; ഒരു value
  type ചെയ്ത് Enter → ആ DPI-ലേക്ക് resample (dialog തുറക്കേണ്ട)
- **Auto** checkbox (default OFF) + target DPI: canvas-ൽ frame drag ചെയ്ത്
  ചെറുതാക്കുമ്പോൾ automatic ആയി resample ആകും (effective DPI target-നേക്കാൾ
  25%-ൽ കൂടുതൽ ആണെങ്കിൽ മാത്രം — repeated quality loss ഒഴിവാക്കാൻ);
  tick ചെയ്യുമ്പോൾ തന്നെ current selection-ന് apply ആകും
- **Embed ചെയ്ത images**: resize കഴിഞ്ഞാൽ .sla-യിലെ base64 data-യും
  ചെറുതാകും (15.1MB → 1.2MB verified)
- Image write fail ആയാൽ കൃത്യമായ കാരണം കാണിക്കും (permission denied
  ഉൾപ്പെടെ); masked adjustment JPEG ആയി save ചെയ്യുമ്പോൾ RGB ആക്കും
- Crop mode ഇപ്പോൾ status bar-ൽ കാണിക്കും (Enter = apply, Esc = cancel),
  button highlight ആകും, selection മാറിയാൽ automatic ആയി exit ചെയ്യും

### 12. Image Editor Improvements
- Selection tools + Hand tool-ന് പുതിയ Photoshop-style icons
- Smart Select (SAM): model load + encode ഇപ്പോൾ background thread-ൽ —
  വലിയ photo-യിലും UI freeze ആകില്ല, വേറെ tool-ലേക്ക് മാറാം
- Filter preview വേഗത്തിലായി: slider drag ചെയ്യുമ്പോൾ stack മുഴുവൻ
  വീണ്ടും കണക്കാക്കില്ല (blur ഉള്ളപ്പോൾ പ്രത്യേകിച്ച്)

### 13. Legacy 1.5.6 File Compatibility (Span Columns)
- 1.5.6-ൽ ഉണ്ടാക്കിയ പഴയ pages ഇപ്പോൾ ശരിയായി open ആകും — headline-കൾ
  വീണ്ടും columns-ന് കുറുകെ span ചെയ്യും
- പഴയ build `FullSpan` എന്ന പേരിലാണ് span columns save ചെയ്തിരുന്നത്,
  1.7.3 `SpanColumns` എന്ന പേര് ഉപയോഗിക്കുന്നു — loader പേര് മാറ്റം
  അറിയാത്തതുകൊണ്ട് എല്ലാ style-ലും Span Columns OFF ആയിപ്പോയി,
  headline single column വീതിയിൽ ഒതുങ്ങി വാക്ക് മുറിഞ്ഞ് overflow ആയി
- ഇപ്പോൾ 150 / 170 / 171 loader-കൾ പഴയ പേരും വായിക്കും (automatic —
  extra option ഒന്നും വേണ്ട). പഴയ `FullSpan="1"` → `SpanColumns=-1`
  (Span All) ആയി map ചെയ്യും
- രണ്ടും ഉണ്ടെങ്കിൽ പുതിയ `SpanColumns`-ന് മുൻഗണന; രണ്ടും ഇല്ലെങ്കിൽ
  പഴയപടി തന്നെ (modern documents-ന് മാറ്റമില്ല)
- Save ചെയ്യുമ്പോൾ എപ്പോഴും പുതിയ പേര് മാത്രം എഴുതും — ഒരിക്കൽ open
  ചെയ്ത് save ചെയ്താൽ file permanent ആയി migrate ആകും
- **Next Style-ഉം** ഇതേ പ്രശ്നത്തിൽ ആയിരുന്നു (പഴയ `nxtStyle` vs പുതിയ
  `NextStyle`) — അതും ഒപ്പം fix ചെയ്തു
- 150 / 170 saver-കളും ഇപ്പോൾ `SpanColumns` + `NextStyle` എഴുതും
  (നേരത്തെ 1.5.x / 1.7.0 ആയി save ചെയ്താൽ ഇവ രണ്ടും നഷ്ടപ്പെട്ടിരുന്നു)
- Verified: യഥാർത്ഥ 1.5.6 page-ൽ 20 headline styles (12 Kicker, 18 M …
  80 B, kickerline) + 33 next-style links restore ആയി; re-save ചെയ്ത്
  വീണ്ടും open ചെയ്താൽ layout pixel-identical

### 14. Legacy Line Spacing Mode (LINESPMODE)
- പഴയ ചില pages-ൽ line spacing mode `LINESPMODE` എന്ന് capital-ൽ
  എഴുതിയിട്ടുണ്ട്, loader വായിച്ചിരുന്നത് `LINESPMode` എന്നും —
  XML-ൽ അക്ഷരത്തിന്റെ case പ്രധാനമാണ്, അതുകൊണ്ട് ആ value drop ആയി
  style default (Fixed) ആയി മാറിയിരുന്നു
- Automatic / Baseline Grid line spacing ഉള്ള പഴയ page open ചെയ്താൽ
  ഒരു warning-ഉം ഇല്ലാതെ layout മാറിപ്പോകുമായിരുന്നു — [[FullSpan]]
  പ്രശ്നത്തിന്റെ അതേ വർഗ്ഗം
- ഇപ്പോൾ ആറ് loader-കളിലും (1.2 / 1.3 / 1.3.4 / 1.5 / 1.7.0 / 1.7.1)
  രണ്ട് spelling-ഉം സ്വീകരിക്കും; രണ്ടും ഉണ്ടെങ്കിൽ പുതിയതിന് മുൻഗണന
- Saver-കൾ പഴയപടി `LINESPMode` മാത്രം എഴുതും — ഒരിക്കൽ open ചെയ്ത്
  save ചെയ്താൽ file normalise ആകും
- Verified: LINESPMODE="1" മാത്രം ഉള്ള file-ൽ 77 style-ഉം Automatic
  ആയി load ആയി; രണ്ട് spelling-ഉം ഉള്ളപ്പോൾ പുതിയത് ജയിച്ചു

### 15. Fix Overflowing Frames (Batch)
- **Extras > Fix Overflowing Frames** + toolbar button (preflight-ന്റെ അടുത്ത്)
- പഴയ 1.5.x pages open ചെയ്യുമ്പോൾ ചില frames 1-4% overflow ആകും;
  ഓരോന്നും കണ്ടുപിടിച്ച് Ctrl+Alt+C ചെയ്യുന്നത് മടുപ്പിക്കുന്ന പണിയാണ്
- Select All + Ctrl+Alt+C പകരം ഉപയോഗിക്കാൻ പറ്റില്ല — അത് ശരിയായി
  ഇരിക്കുന്ന frames-ഉം ചെറുതാക്കും, grid നശിക്കും
- ഈ command **overflow ഉള്ള frames മാത്രം വലുതാക്കും** (grow-only);
  ബാക്കിയെല്ലാം saved size-ൽ തന്നെ നിൽക്കും
- Scope: current page (selection ഉണ്ടെങ്കിൽ selected frames മാത്രം).
  Whole document-ന് പ്രത്യേക menu entry ഉണ്ട്
- Skip ചെയ്യുന്നവ: chain-ന്റെ അവസാനത്തേത് അല്ലാത്ത frames, note frames,
  group-നുള്ളിലെ frames, lock ചെയ്ത frames, master page mode
- **10%-ൽ കൂടുതൽ വളരേണ്ട frames വളർത്തില്ല** — പകരം പേര് സഹിതം
  report ചെയ്യും (അത് editorial തീരുമാനമാണ്, layout fix അല്ല).
  "Select Them" അമർത്തിയാൽ ആ frames select ആകും
- മുഴുവൻ batch-ഉം **ഒറ്റ undo** — ഒരു Ctrl+Z-ൽ എല്ലാം പഴയപടി ആകും
- ഒരിക്കലും automatic ആയി run ആകില്ല (1863b8f policy)
- Verified: യഥാർത്ഥ 1.5.6 page-ൽ (46 text frames) "Fixed 3 overflowing
  frame(s); 1 skipped (would grow more than 10%)" — 3 എണ്ണം വളർന്നു
  (+3.9%, +4.4%, +4.8%), 42 എണ്ണം തൊട്ടില്ല, ഒന്നും ചെറുതായില്ല,
  ഒറ്റ undo-യിൽ എല്ലാം restore ആയി

### 16. Undo Improvements
- **Apply Style Chain**: നേരത്തെ Ctrl+Z-ൽ ഒട്ടും undo ആകുമായിരുന്നില്ല —
  StoryText-ന് സ്വന്തമായി undo record ഇല്ല, അതുകൊണ്ട് style apply
  ചെയ്തത് undo stack-ൽ എത്തിയിരുന്നില്ല. ഇപ്പോൾ ഒറ്റ undo step
- **Auto Fit Height (Ctrl+Alt+C)**: പല frames select ചെയ്ത് fit
  ചെയ്താൽ ഓരോ frame-നും ഓരോ undo step ആയിരുന്നു (12 frames = 12 തവണ
  Ctrl+Z). ഇപ്പോൾ മുഴുവൻ selection-ഉം ഒറ്റ Ctrl+Z
- രണ്ടും pure grouping/recording ആണ് — behaviour-ൽ മാറ്റമില്ല

### 17. News Browser — Paragraph Styles Docker-ൽ ഒരു Tab ആയി
- Paragraph Styles docker-ന്റെ tab bar ഇപ്പോൾ:
  **[ Styles ] [ Design Style ] [ News Browser ]**
- നേരത്തെ News Browser പുറത്തെ dock tab bar-ൽ ആയിരുന്നു — ഒരേ panel-ന്
  രണ്ട് tab bar. ഇപ്പോൾ ഒറ്റ tab bar
- **SR Menu > Enable News Browser Panel** setting പഴയപടി പ്രവർത്തിക്കും:
  OFF ആണെങ്കിൽ tab കാണിക്കില്ല (ശൂന്യമായ tab അല്ല, tab തന്നെ ഇല്ല)
- ഇപ്പോൾ setting മാറ്റിയാൽ **ഉടനെ** tab വരും/പോകും — restart വേണ്ട
  (നേരത്തെ restart വേണമായിരുന്നു, പക്ഷേ അത് എവിടെയും പറഞ്ഞിരുന്നില്ല)
- News Browser-ന്റെ എല്ലാ പ്രവർത്തനവും അതേപടി: edition/page/date,
  Fetch News, Select All, place — ഒന്നും മാറിയിട്ടില്ല
- Tab മാറുമ്പോൾ docker-ന്റെ വീതി മാറില്ല (scroll area ഉപയോഗിക്കുന്നു)
- Windows menu-വിലെ "News Browser" ഇപ്പോൾ ആ tab-ലേക്ക് കൊണ്ടുപോകും
- പഴയ layout-കളും fresh install-ഉം പഴയപടി open ആകും

### 18. In-App Help (? Buttons)
- Panel-കളുടെ tab bar-ന്റെ വലതു മൂലയിൽ ഒരു ചെറിയ **?** button
- അമർത്തിയാൽ ആ tab-ന്റെ സഹായം നേരിട്ട് തുറക്കും (help text English-ൽ)
- മൂന്ന് ? button, ഏഴ് വിഷയം:
  - **Paragraph Styles docker** — Styles / Design Style / News Browser
  - **Style Manager** — Paragraph Rules / Paragraph Shading /
    Nested Styles / Image Bullets
  - **News Browser** panel-ന്റെ header-ൽ ഒരെണ്ണം
- എല്ലാം ഒരേ help window തന്നെ ഉപയോഗിക്കും; വീണ്ടും അമർത്തിയാൽ
  പുതിയ window വരില്ല, ഉള്ളത് തന്നെ ആ ഭാഗത്തേക്ക് പോകും
- ഉള്ളടക്കം compositor-ന് വേണ്ടിയാണ്: എന്ത് ചെയ്യും, എപ്പോൾ ഉപയോഗിക്കും,
  step by step, തെറ്റിയാൽ എന്ത് നോക്കണം
- News Browser-ന്റെ ഭാഗത്ത് **API URL** ആണ് പറയുന്നത് —
  ഇത് database connection അല്ല, web API ആണ്
- Help text ഒരു **HTML file** ആണ്:
  `/usr/local/share/scribus/help/faircode/faircode-help.html`.
  അത് edit ചെയ്താൽ മതി — Scribus വീണ്ടും build ചെയ്യേണ്ട

### 19. Crash Capture (.deb package-ൽ)
- പുതിയ ഒരു machine-ൽ `.deb` install ചെയ്താൽ crash logging **തനിയെ** set
  ആകും. ഇനി ഓരോ machine-ലും കൈകൊണ്ട് ഒന്നും ഉണ്ടാക്കേണ്ട
- Application menu-ൽ **"Scribus (Crash Capture)"** എന്ന ഒരു entry വരും.
  അത് വഴി തുറന്നാൽ Scribus gdb-യുടെ ഉള്ളിൽ ഓടും
- Crash ആയാൽ backtrace ഇവിടെ വരും:
  `~/scribus-crashlogs/crash-<time>.log` — ഇത് **ഒരിക്കലും delete ആകില്ല**
- സാധാരണ session-ന്റെ log: `~/scribus-crashlogs/runs/<date>/session-*.log`
  — 7 ദിവസം കഴിഞ്ഞാൽ തനിയെ പോകും (2 GB cap-ഉം ഉണ്ട്)
- പഴയ log തനിയെ വൃത്തിയാക്കാൻ ഒരു daily systemd timer install ആകും
- Install ചെയ്ത user-ന് `.sla` file double-click ചെയ്താൽ Crash Capture
  വഴി തുറക്കും
- **സാധാരണ `scribus` launcher-ന് ഒരു മാറ്റവുമില്ല.** Crash capture
  വേണ്ടെങ്കിൽ പഴയതുപോലെ `scribus` ഉപയോഗിച്ചാൽ മതി
- Uninstall ചെയ്താലും `~/scribus-crashlogs` **delete ആകില്ല** —
  backtrace-കൾ നഷ്ടപ്പെടരുത്
- ശ്രദ്ധിക്കുക: crash capture-ൽ Scribus-ന്റെ സ്വന്തം emergency save
  പ്രവർത്തിക്കില്ല. അതുകൊണ്ട് **autosave on ആയിരിക്കണം**
- gdb വേണം (`apt install gdb`). ഇല്ലെങ്കിൽ scribus-debug
  ഒരു message കാണിച്ച് നിർത്തും

### 20. Kasm Workspace — Browser-ൽ Scribus

- Scribus ഇനി **browser-ൽ** ഉപയോഗിക്കാം. User-ന്റെ machine-ൽ ഒന്നും
  install ചെയ്യേണ്ട — Kasm Workspaces server-ൽ ഓടും, screen browser-ലേക്ക്
  stream ചെയ്യും
- Image ഉണ്ടാക്കാൻ:
  ```bash
  docker build -f kasm/Dockerfile.kasm -t scribus-mdtp:kasm .
  ```
  അല്ലെങ്കിൽ locally test ചെയ്യാൻ `./kasm/test-local.sh` — എന്നിട്ട്
  `https://localhost:6901` (user: `kasm_user`, password: `password`)
- Session തുറന്നാൽ **Scribus തനിയെ വരും**. വേറെ ഒന്നും ചെയ്യേണ്ട
- **Malayalam പൂർണ്ണമായി വർക്ക് ചെയ്യും**: SMC fonts, Lohit, Samyak, Noto
  എല്ലാം ഉണ്ട്. Keyboard-ൽ **Deshabhimani layout തന്നെയാണ് default**
  (`m17n_ml_deshabhimani`) — Telugu Praja-യും ഉണ്ട്
- **SAM Smart Select** പ്രവർത്തിക്കും. Model files `/opt/scribus/sam`-ൽ
  ഇടണം, അല്ലെങ്കിൽ build ചെയ്യുമ്പോൾ
  `--build-arg INCLUDE_SAM_MODELS=true` കൊടുക്കണം
- ശ്രദ്ധിക്കുക: ഈ laptop-ൽ build ചെയ്യുമ്പോൾ `--network=host` വേണം
  (VPN കാരണം docker build-ന് DNS കിട്ടില്ല). `test-local.sh` ഇത്
  തനിയെ ശരിയാക്കും
- വിശദമായ വിവരങ്ങൾ `kasm/README.md`-ൽ ഉണ്ട്

### 21. Image Eraser (Shift+E) — Placed image-ൽ paint ചെയ്ത് മായ്ക്കാം

- Page-ൽ ഇട്ട ഒരു photo-യുടെ വേണ്ടാത്ത ഭാഗം **brush കൊണ്ട് paint ചെയ്ത്
  മായ്ക്കാം**. Photoshop-ലെ eraser പോലെ തന്നെ
- **Original image file-ന് ഒരു മാറ്റവുമില്ല.** മായ്ച്ചത് ഒരു mask ആയി
  `.sla`-ൽ മാത്രം സൂക്ഷിക്കും. അതുകൊണ്ട് എപ്പോൾ വേണമെങ്കിലും
  തിരികെ കൊണ്ടുവരാം
- Image frame select ചെയ്ത ശേഷം toolbar-ലെ eraser button, അല്ലെങ്കിൽ
  **Shift+E**. Image frame select ചെയ്തിട്ടില്ലെങ്കിൽ button enable ആകില്ല
- Tool on ആയാൽ മുകളിൽ ഒരു ചെറിയ **options bar** വരും:
  - **Brush** — 1 മുതൽ 500 px വരെ. Canvas-ൽ `[` `]` keys കൊണ്ടും മാറ്റാം
  - **Hardness** — 0% (നല്ല soft edge) മുതൽ 100% (hard edge) വരെ.
    Photo-യെ page-ലേക്ക് blend ചെയ്യാൻ soft edge ഉപയോഗിക്കുക
  - **Reset Erasure** — ആ frame-ലെ മായ്ച്ചതെല്ലാം ഒറ്റയടിക്ക് തിരികെ
- **Alt പിടിച്ച് വരച്ചാൽ മായ്ച്ചത് തിരികെ വരും** (un-erase). Cursor-ൽ
  ഒരു `+` അടയാളം കാണിക്കും
- Mouse pointer-ന്റെ കൂടെ brush-ന്റെ വട്ടം കാണാം. Soft brush ആണെങ്കിൽ
  ഉള്ളിൽ ഒരു dotted വട്ടം കൂടി — അതാണ് solid ആയ ഭാഗം
- **Undo/Redo പ്രവർത്തിക്കും.** ഒരു stroke = ഒരു undo step
- **PDF-ലും print-ലും ശരിയായി വരും:**
  - PDF 1.4/1.5/1.6/X-4 — soft edge അതേപടി (8-bit soft mask)
  - PDF/X-1a, X-3 (newspaper preset) — മായ്ച്ചത് വരും, പക്ഷേ soft edge
    hard ആയി മാറും. ആ PDF version-കളിൽ transparency അനുവദനീയമല്ല
  - PostScript print — soft edge അതേപടി
- Esc അമർത്തിയാൽ tool-ൽ നിന്ന് പുറത്തുവരാം
- ശ്രദ്ധിക്കുക: **Image Effects dialog-ന്റെ preview-യിൽ മായ്ച്ചത്
  കാണില്ല.** പക്ഷേ OK കൊടുത്താലും mask നഷ്ടപ്പെടില്ല — canvas-ൽ
  ശരിയായി കാണാം

### 22. Detect Contour from Image (Ctrl+Shift+K) — Text wrap ചിത്രത്തിന്റെ ശരിക്കുള്ള shape-ന് ചുറ്റും

- Image eraser കൊണ്ട് photo-യുടെ ഭാഗങ്ങൾ മായ്ച്ചതിന് ശേഷം, **ബാക്കി
  കാണുന്ന ഭാഗത്തിന്റെ outline തനിയെ കണ്ടെത്തി** contour line ആക്കാം.
  Text അപ്പോൾ frame-ന്റെ ചതുരത്തിന് ചുറ്റുമല്ല, **ചിത്രത്തിന്റെ ശരിക്കുള്ള
  രൂപത്തിന് ചുറ്റും** ഒഴുകും
- Image frame select ചെയ്യുക → **Item › Shape & Paths › Detect Contour from
  Image**, അല്ലെങ്കിൽ **Ctrl+Shift+K**. Properties palette-ലെ **Shape** tab-ൽ
  "Contour: Detect from Image" button-ഉം ഉണ്ട്
- മായ്ച്ചിട്ടില്ലെങ്കിലും, ചിത്രത്തിന് സ്വന്തം transparency (cut-out PNG)
  ഉണ്ടെങ്കിൽ അതും ഉപയോഗിക്കും. രണ്ടും ഇല്ലെങ്കിൽ button enable ആകില്ല
- Dialog-ൽ:
  - **Preview** — mask-ഉം കണ്ടെത്തിയ outline-ഉം **ചുവപ്പ് നിറത്തിൽ**
    (holes ഓറഞ്ച് നിറത്തിൽ). Apply ചെയ്യുന്നതിന് മുൻപ് കാണാം
  - **Alpha threshold** (1–254, default 128) — എത്ര transparent ആയാൽ
    "മായ്ച്ചു" എന്ന് കണക്കാക്കണം
  - **Simplify tolerance** (0.5–20 px, default 2.0) — കൂട്ടിയാൽ nodes കുറയും.
    Node കൂടുതലായാൽ layout മന്ദഗതിയിലാകും; 2000-ന് മുകളിൽ ആയാൽ
    മുന്നറിയിപ്പ് വരും
  - **Contour** — "Largest region only" (default) / "All separate regions" /
    "Include holes"
  - **Use the contour for text wrap** (default on) — ഇത് ഓഫ് ആക്കിയാൽ
    contour സൂക്ഷിക്കും, പക്ഷേ text wrap മാറില്ല
- **Include holes** ഉപയോഗിച്ചാൽ ചിത്രത്തിനുള്ളിലെ ദ്വാരത്തിലും text
  ഒഴുകും — verify ചെയ്തിട്ടുണ്ട്
- **Undo/Redo പ്രവർത്തിക്കും.** Contour-ഉം text wrap mode-ഉം ഒരുമിച്ച്
  ഒറ്റ step ആയി undo ആകും
- **⚠️ ശ്രദ്ധിക്കുക:** Scribus-ൽ text wrap പ്രവർത്തിക്കണമെങ്കിൽ **image
  frame, text frame-ന്റെ മുകളിൽ** ആയിരിക്കണം. അല്ലെങ്കിൽ contour ശരിയായി
  കണ്ടെത്തിയാലും text ഒഴുകില്ല. `Item › Level › Move to Front` ഉപയോഗിക്കുക

### 23. Authors
- Newspaper Page Layout: Suneer. A (alp.suneer@gmail.com)

---

## Build Command
```bash
cd /home/s1/suneer/scribus-1.7.3/build
make -j$(nproc)
sudo make install
```

## Run Command
```bash
scribus
# (fcitx5 auto-configured via wrapper)
```

## Backup Location
`/home/s1/suneer/backup/scribus-1.7.3-DDMMYY-HHMM`
