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

### 23. Remove Object (Shift+R) — വേണ്ടാത്ത സാധനം paint ചെയ്ത് മായ്ക്കാം, പിന്നിലുള്ളത് താനെ വരും

- Photo-യിൽ വേണ്ടാത്ത ഒരു സാധനത്തിന് മുകളിൽ **brush കൊണ്ട് ചുവപ്പ് paint
  ചെയ്ത് Apply അമർത്തുക**. ആ pixel-കൾ ചുറ്റുമുള്ള pixel-കളിൽ നിന്ന്
  **പുതുതായി ഉണ്ടാക്കി** നിറയ്ക്കും. Wire, pole, ചെറിയ ശല്യങ്ങൾ ഒക്കെ
  മാഞ്ഞുപോകും
- **Image Eraser-ഉം ഇതും രണ്ടാണ്** — അത് pixel മറയ്ക്കുക മാത്രം ചെയ്യുന്നു
  (എപ്പോൾ വേണമെങ്കിലും തിരികെ കൊണ്ടുവരാം). ഇത് **പുതിയ pixel ഉണ്ടാക്കുന്നു**
- **Original image file-ന് ഒരു മാറ്റവുമില്ല.** ഫലം `.sla`-ന്റെ അടുത്ത്
  `.scribus_edits/` folder-ൽ ഒരു **പുതിയ PNG** ആയി വരും
  (`photo_inpaint_20260828_195037.png` എന്ന മട്ടിൽ), frame ആ പുതിയ file-ലേക്ക്
  മാറും. പഴയ file അതേപടി ഇരിക്കും
- Image frame select ചെയ്ത ശേഷം toolbar-ലെ button (eraser + നക്ഷത്രം),
  **Shift+R**, അല്ലെങ്കിൽ `Item › Shape & Paths › Remove Object`
- മുകളിലെ **options bar**:
  - **Brush** — 1 മുതൽ 500 px വരെ. Canvas-ൽ `[` `]` keys കൊണ്ടും മാറ്റാം
  - **Hardness** — 0% മുതൽ 100% വരെ
  - **Clear Mask** — paint ചെയ്തതെല്ലാം കളയാൻ
  - **Apply** — എന്തെങ്കിലും paint ചെയ്താൽ മാത്രമേ enable ആകൂ
- **Alt പിടിച്ച് വരച്ചാൽ mask മായ്ക്കാം** (unpaint).
  **Shift+click ചെയ്താൽ** കഴിഞ്ഞ stroke-ൽ നിന്ന് നേർവരയായി തുടരും
- Apply അമർത്തിയാൽ **"Removing object..." dialog** വരും — progress bar, എത്ര
  സെക്കൻഡ് ആയി എന്ന കണക്ക്, **Cancel** button. Dialog block ചെയ്യില്ല,
  അതിനിടയിലും page കാണാം
- **Cancel അമർത്തിയാൽ ഒന്നും മാറില്ല**, paint ചെയ്ത mask അതേപടി ഇരിക്കും —
  അല്പം മാറ്റി വീണ്ടും ശ്രമിക്കാം
- **Undo/Redo പ്രവർത്തിക്കും.** മുഴുവൻ operation-ഉം **ഒറ്റ undo step**
  ("Remove object from image"), undo ചെയ്താൽ പഴയ image തിരികെ വരും
- **രണ്ട് രീതികൾ ഉണ്ട്, Scribus താനെ ശരിയായത് തിരഞ്ഞെടുക്കും:**
  - **നേർത്ത സാധനങ്ങൾ** (wire, antenna, ചെറിയ പോറൽ, പൊടി) അല്ലെങ്കിൽ
    **ആകാശം/ഭിത്തി പോലെ ഒരേപോലുള്ള background** — വേഗതയേറിയ രീതി.
    ഏതാനും milliseconds മതി
  - **വീതിയുള്ള ഭാഗം + design/texture ഉള്ള background** (ആൾക്കൂട്ടം, വസ്ത്രം,
    പുല്ല്, ഇലകൾ) — ചിത്രത്തിന്റെ **മറ്റു ഭാഗങ്ങളിൽ നിന്ന് യഥാർത്ഥ
    കഷണങ്ങൾ പകർത്തി** നിറയ്ക്കുന്ന രീതി. കുറച്ച് സമയമെടുക്കും
    (4000x3000 photo-യിൽ ഒരാളുടെ വലുപ്പം ≈ 4 സെക്കൻഡ്), പക്ഷേ ഫലം
    **വളരെ മെച്ചം**
- **മുമ്പത്തെ "കുഴഞ്ഞ പോലെ" (blur/smear) പ്രശ്നം പരിഹരിച്ചു.** ആൾക്കൂട്ടത്തിന്റെ
  ഫോട്ടോയിൽ അളന്നു നോക്കിയപ്പോൾ, നിറച്ച ഭാഗത്തിന്റെ വിശദാംശം (detail)
  ചുറ്റുമുള്ളതിന്റെ **41%** മാത്രമായിരുന്നു — ഇപ്പോൾ **85%**. അതായത്
  നിറച്ച ഭാഗം ചുറ്റുമുള്ളതു പോലെ തന്നെ കാണപ്പെടും
- **⚠️ എന്ത് പ്രതീക്ഷിക്കാം, എന്ത് പ്രതീക്ഷിക്കരുത്:**
  - ✅ **നന്നായി പ്രവർത്തിക്കും:** ചെറുതും ഇടത്തരവുമായ സാധനങ്ങൾ, wire,
    pole, ചെറിയ ശല്യങ്ങൾ, texture ഉള്ള background-ൽ ഉള്ളവ. അരികുകളും
    (edges) വരകളും മുറിയാതെ തുടരും, നിറങ്ങൾ കൂടിക്കലരില്ല
  - ⚠️ **വലിയ complex സാധനങ്ങൾ** (ഒരാളെ മുഴുവൻ) മായ്ച്ചാൽ: ഇനി
    കുഴഞ്ഞ പോലെ വരില്ല, പക്ഷേ ചിത്രത്തിലെ **മറ്റു ഭാഗങ്ങൾ ആവർത്തിച്ചു
    വരാം** (ഉദാ: ഒരു കൊടി രണ്ടു തവണ). സൂക്ഷിച്ചു നോക്കിയാൽ കാണാം
  - ❌ **ഇത് AI അല്ല.** മറഞ്ഞിരുന്ന മുഖമോ ശരീരമോ ഉണ്ടാക്കാൻ ഇതിന് കഴിയില്ല —
    ഫോട്ടോയിൽ ഇല്ലാത്ത ഒന്നും ഇത് കണ്ടുപിടിക്കില്ല. അത് ചെയ്യുന്നത്
    **അതേ ഫോട്ടോയിലെ യഥാർത്ഥ കഷണങ്ങൾ** കൊണ്ട് നിറയ്ക്കുക എന്നതാണ്
  - വലിയ ഭാഗം ഒറ്റയടിക്ക് ചെയ്യുന്നതിനു പകരം **ചെറിയ ഭാഗങ്ങളായി പല തവണ**
    ചെയ്യുന്നതാണ് ഇപ്പോഴും നല്ലത്
- Internet വേണ്ട, extra software വേണ്ട, model download വേണ്ട — എല്ലാം
  Scribus-ന്റെ ഉള്ളിൽ തന്നെ
- **⚠️ Paper-ൽ print ചെയ്ത് പരിശോധിച്ചിട്ടില്ല.** Screen-ലും PDF-ലും
  ശരിയാണ്; അച്ചടിച്ച് ഒന്ന് നോക്കണം

### 24. Apply (Best Quality) — AI ഉപയോഗിച്ച് കൂടുതൽ നന്നായി മായ്ക്കാം

- Remove Object tool-ൽ ഇപ്പോൾ **രണ്ട് Apply button** ഉണ്ട്:
  - **Apply (Fast)** — പഴയതു തന്നെ. Scribus-ന്റെ ഉള്ളിലെ രീതി, ഉടനെ ഫലം.
    മിക്ക ജോലിക്കും ഇതു മതി
  - **Apply (Best Quality)** — നിങ്ങളുടെ സ്വന്തം കമ്പ്യൂട്ടറിൽ ഓടുന്ന
    **IOPaint (LaMa)** എന്ന AI model ഉപയോഗിക്കും. കുറച്ച് സമയമെടുക്കും,
    പക്ഷേ **ആൾക്കൂട്ടം, ആളുകൾ, വസ്ത്രം, texture ഉള്ള background**
    എന്നിവയിൽ വളരെ മെച്ചപ്പെട്ട ഫലം
- **ഇത് default ആയി ഓഫ് ആണ്.** ഉപയോഗിക്കാൻ:
  1. `File › Preferences › AI Services` തുറക്കുക
  2. **Enable AI features** ടിക്ക് ചെയ്യുക
  3. **Test Connection** അമർത്തി പച്ച ✓ വരുന്നുണ്ടെന്ന് ഉറപ്പാക്കുക
- **ആദ്യം IOPaint install ചെയ്ത് ഓടിക്കണം** (ഒരു തവണ മാത്രം):
  ```
  pip install iopaint
  iopaint start --model=lama --port=8080
  ```
  ഇത് ഓടിക്കൊണ്ടിരിക്കണം, അല്ലെങ്കിൽ Best Quality button പ്രവർത്തിക്കില്ല
- **ചിത്രം ഇന്റർനെറ്റിലേക്ക് പോകില്ല.** എല്ലാം നിങ്ങളുടെ സ്വന്തം
  കമ്പ്യൂട്ടറിൽ തന്നെ. Preferences-ൽ കൊടുത്ത വിലാസത്തിലേക്ക് മാത്രം,
  വേറെ ഒരിടത്തേക്കും അല്ല
- Button പ്രവർത്തിക്കുന്നില്ലെങ്കിൽ **അതിന്റെ മുകളിൽ mouse വെച്ചാൽ കാരണം
  പറയും** — "Preferences-ൽ ഓൺ ചെയ്യുക", "IOPaint ഓടുന്നില്ല", അല്ലെങ്കിൽ
  "ആദ്യം mask വരയ്ക്കുക"
- Mask ചിത്രത്തിന്റെ **5%-ൽ കൂടുതൽ** ആണെങ്കിൽ button-ന്റെ അടുത്ത്
  *"Large area - Best Quality recommended"* എന്ന് ചെറുതായി കാണിക്കും
- **Undo പ്രവർത്തിക്കും** — "Remove object from image (AI)" എന്ന പേരിൽ,
  Fast രീതിയുടേതിൽ നിന്ന് വേറിട്ട്
- ഫലം `.scribus_edits/` ഫോൾഡറിൽ **`_lama_`** എന്ന പേരോടെ വരും
  (Fast രീതിയുടേത് `_inpaint_`). **പഴയ ചിത്രത്തിന് ഒരു മാറ്റവുമില്ല**
- **⚠️ ശ്രദ്ധിക്കുക:**
  - GPU ഇല്ലാത്ത കമ്പ്യൂട്ടറിൽ വലിയ ഭാഗത്തിന് **30-60 സെക്കൻഡ്** വരെ
    എടുക്കാം. 10 സെക്കൻഡ് കഴിഞ്ഞാൽ dialog അത് പറയും. Cancel ചെയ്യാം
  - Cancel ചെയ്താലും **mask നഷ്ടപ്പെടില്ല** — വീണ്ടും ശ്രമിക്കാം,
    അല്ലെങ്കിൽ Apply (Fast) ഉപയോഗിക്കാം
  - എന്തെങ്കിലും തകരാറുണ്ടെങ്കിൽ **server പറഞ്ഞ കാരണം തന്നെ** കാണിക്കും

### 25. OpenRouter — ഇന്റർനെറ്റിലെ AI model-കളും ഉപയോഗിക്കാം (പണം ചെലവാകും)

- `File › Preferences › AI Services`-ൽ ഇപ്പോൾ **Provider** എന്ന ഒരു
  dropdown ഉണ്ട്. രണ്ട് വഴി:
  - **LaMa (local, free)** — പഴയതു തന്നെ. സ്വന്തം കമ്പ്യൂട്ടറിൽ,
    സൗജന്യം, ചിത്രം പുറത്തേക്ക് പോകില്ല
  - **OpenRouter (cloud, paid)** — ഇന്റർനെറ്റിലെ AI model-കൾ.
    **ചിത്രം പുറത്തേക്ക് പോകും, ഓരോ തവണയും പണം ചെലവാകും**
- **എന്തിനാണ് ഇത്:** ഒരു API key കൊടുത്താൽ Google, OpenAI, FLUX,
  Seedream തുടങ്ങി **പല കമ്പനികളുടെ model-കൾ** ഉപയോഗിക്കാം. ഒരു model
  ഒരു ചിത്രം ചെയ്യാൻ വിസമ്മതിച്ചാൽ **വേറൊരു model തിരഞ്ഞെടുത്താൽ മതി**
- ഉപയോഗിക്കാൻ:
  1. <https://openrouter.ai/keys> -ൽ ഒരു API key ഉണ്ടാക്കുക
     (ഏകദേശം **$10** credit ഇടണം)
  2. Provider → **OpenRouter** തിരഞ്ഞെടുക്കുക
  3. **API key** ഒട്ടിക്കുക. **Show** ടിക്ക് ചെയ്താൽ ശരിയായോ എന്ന്
     നോക്കാം
  4. **Model** തിരഞ്ഞെടുക്കുക (default: **Nano Banana 2** — നല്ല
     ഫലവും വിലയും ഒരുമിച്ച്)
  5. **Test Connection** അമർത്തുക. ശരിയാണെങ്കിൽ **എത്ര credit
     ബാക്കിയുണ്ട്** എന്നു കൂടി പറയും
- **Model എപ്പോൾ വേണമെങ്കിലും മാറ്റാം** — key വീണ്ടും കൊടുക്കേണ്ട,
  Scribus വീണ്ടും തുറക്കുകയും വേണ്ട. ആറ് model ഉണ്ട്:
  Nano Banana 2 / 2 Lite / Pro, GPT Image 2, FLUX 2 Flex, Seedream 4.5.
  ഓരോന്നിന്റെയും അടിയിൽ **എപ്പോൾ ഏതു വേണം** എന്ന് ചെറുതായി എഴുതിയിട്ടുണ്ട്
- **വില:** ഒരു തവണയ്ക്ക് ഏകദേശം **$0.03–0.15 (₹3–13)**
- **ഏത് model ചെയ്തു എന്ന് മൂന്നിടത്ത് കാണാം:**
  - ജോലി നടക്കുമ്പോൾ dialog-ൽ — *"Inpainting with Nano Banana 2 ..."*
  - Undo-യിൽ — *"Remove Object (OpenRouter: Nano Banana 2)"*
  - File-ന്റെ പേരിൽ — `_openrouter_seedream_4.5_...png`.
    അതുകൊണ്ട് **രണ്ട് model-ന്റെ ഫലം വെവ്വേറെ കിട്ടും, താരതമ്യം ചെയ്യാം**
- **Model വിസമ്മതിച്ചാൽ** (ചില ചിത്രങ്ങൾ ചെയ്യാൻ അവ കൂട്ടാക്കില്ല):
  - **അത് പറഞ്ഞ കാരണം അതേപടി** കാണിക്കും
  - **വേറൊരു model താനെ ഉപയോഗിക്കില്ല** — ഏത് model എന്നത്
    നിങ്ങളുടെ തീരുമാനം തന്നെ
  - **Mask നഷ്ടപ്പെടില്ല** — Preferences-ൽ FLUX 2 Flex-ഓ Seedream 4.5-ഓ
    തിരഞ്ഞെടുത്ത് വീണ്ടും ശ്രമിക്കാം, അല്ലെങ്കിൽ LaMa (ഇതിന് ഒരു
    നിയന്ത്രണവുമില്ല)
- **⚠️ ശ്രദ്ധിക്കുക:**
  - **ചിത്രം OpenRouter-ലേക്കും അവിടെ നിന്ന് model കമ്പനിയുടെ
    server-ലേക്കും പോകും.** രഹസ്യമായി വെക്കേണ്ട ചിത്രമാണെങ്കിൽ
    **LaMa ഉപയോഗിക്കുക**
  - Key `scribus172.rc`-ൽ **base64** ആയി സൂക്ഷിക്കും. അത് കാഴ്ചയിൽ
    മറയ്ക്കാൻ മാത്രം; ആ ഫയൽ വായിക്കാൻ കഴിയുന്നവർക്ക് key കിട്ടും.
    **ആ ഫയലിനെ key പോലെ തന്നെ കരുതുക**
  - Key തെറ്റാണെങ്കിൽ / credit തീർന്നാൽ **എന്താണ് ചെയ്യേണ്ടത് എന്ന്
    കൃത്യമായി പറയും**
  - **ഇതുവരെ ശരിക്കുള്ള ഒരു key വെച്ച് ഒരു ചിത്രവും ചെയ്തുനോക്കിയിട്ടില്ല.**
    Key ശരിയാണോ എന്ന പരിശോധന മാത്രമേ ശരിക്കുള്ള server-ൽ നടത്തിയിട്ടുള്ളൂ

### 26. Google Gemini — UPI വഴി പണം അടയ്ക്കാവുന്ന AI (പുതിയത്)

- `File › Preferences › AI Services`-ലെ **Provider** dropdown-ൽ ഇപ്പോൾ
  **മൂന്നാമതൊരു വഴി** കൂടിയുണ്ട്:
  **Google Gemini (direct, UPI)**
- **എന്തിനാണ് ഇത്:** OpenRouter-ന് **വിദേശത്ത് പ്രവർത്തിക്കുന്ന card**
  വേണം. Google AI Studio-ൽ **UPI വഴി രൂപയിൽ (GST ഉൾപ്പെടെ)** പണം
  അടയ്ക്കാം. **Model-കൾ ഏതാണ്ട് അതേ Nano Banana തന്നെ** — മാറുന്നത്
  ഫലമല്ല, **പണം അടയ്ക്കുന്ന വഴി മാത്രമാണ്**
- ഉപയോഗിക്കാൻ:
  1. <https://aistudio.google.com> -ൽ ഒരു API key ഉണ്ടാക്കുക
     (**billing on ആക്കണം**)
  2. Provider → **Google Gemini (direct, UPI)** തിരഞ്ഞെടുക്കുക
  3. **API key** ഒട്ടിക്കുക. **Show** ടിക്ക് ചെയ്താൽ ശരിയായോ എന്ന്
     നോക്കാം
  4. **Model** തിരഞ്ഞെടുക്കുക (default: **Nano Banana 2**)
  5. **Test Connection** അമർത്തുക. **ഇത് ഒരു പൈസയും ചെലവാക്കില്ല** —
     key ശരിയാണോ എന്നു മാത്രം നോക്കും
- **മൂന്ന് model:** Nano Banana 2 (സാധാരണ), 2 Lite (ഏറ്റവും വില
  കുറഞ്ഞത്), Pro (ഏറ്റവും നല്ലത്, ഏകദേശം ഇരട്ടി വില)
- **വില:** ഒരു തവണയ്ക്ക് ഏകദേശം **$0.05–0.15 (₹4–13)**
- **ഏത് ചെയ്തു എന്ന് മൂന്നിടത്ത് കാണാം** (OpenRouter പോലെ തന്നെ):
  - dialog-ൽ — *"Inpainting with Nano Banana 2 (Gemini) ..."*
  - Undo-യിൽ — *"Remove Object (Gemini: Nano Banana 2)"*
  - File-ന്റെ പേരിൽ — `_gemini_gemini_3.1_flash_image_...png`
- **Key തെറ്റാണെങ്കിൽ** *"Invalid Gemini API key..."* എന്ന് കൃത്യമായി
  പറയും. **Billing on അല്ലെങ്കിൽ** aistudio.google.com-ൽ അത് ശരിയാക്കാൻ
  പറയും
- **⚠️ ശ്രദ്ധിക്കുക:**
  - **ചിത്രം Google-ന്റെ server-ലേക്ക് പോകും.** രഹസ്യമായി വെക്കേണ്ട
    ചിത്രമാണെങ്കിൽ **LaMa ഉപയോഗിക്കുക**
  - Key `scribus172.rc`-ൽ **base64** ആയി സൂക്ഷിക്കും — OpenRouter
    key പോലെ തന്നെ. **ആ ഫയലിനെ key പോലെ കരുതുക**
  - **ഒരു provider തകരാറിലായാൽ Scribus താനെ വേറൊന്നിലേക്ക് മാറില്ല.**
    ഏത് service എന്നത് നിങ്ങളുടെ തീരുമാനം തന്നെ
  - **ഇതുവരെ ശരിക്കുള്ള ഒരു key വെച്ച് ഒരു ചിത്രവും ചെയ്തുനോക്കിയിട്ടില്ല.**
    Google-ന്റെ server-ൽ പരിശോധിച്ചത് **തെറ്റായ ഒരു key വെച്ച് മാത്രമാണ്**

### 27. AI Text Tools — അടിക്കുറിപ്പ്, തലക്കെട്ട്, പരിഭാഷ (പുതിയത്)

- `Item › AI Text Tools` എന്ന പുതിയ menu. **ആറ് കാര്യങ്ങൾ**:
  - **Generate Caption** — ചിത്രത്തിന് അടിക്കുറിപ്പ്
    (**മലയാളത്തിലും ഇംഗ്ലീഷിലും ഒരുമിച്ച്**)
  - **Generate Alt Text** — ചിത്രത്തിന്റെ വിവരണം
  - **Suggest Headlines** — വാർത്തയ്ക്ക് **3 തലക്കെട്ടുകൾ**
  - **Summarize Article** — ചുരുക്കം
  - **Translate...** — പരിഭാഷ (മലയാളം, ഇംഗ്ലീഷ്, ഹിന്ദി, തമിഴ്,
    കന്നഡ, തെലുങ്ക്, അറബി തുടങ്ങിയവ)
  - **Improve Text** — വ്യാകരണവും ശൈലിയും നന്നാക്കുക
- **ചിത്രത്തിന്റെ frame** തിരഞ്ഞെടുത്താൽ ആദ്യത്തെ രണ്ടെണ്ണം,
  **എഴുത്തിന്റെ frame** തിരഞ്ഞെടുത്താൽ ബാക്കി നാലെണ്ണം —
  **ബാക്കിയുള്ളവ മങ്ങിക്കിടക്കും**
- **എവിടെ set ചെയ്യണം:** `File › Preferences › AI Services` →
  **Text AI** എന്ന പുതിയ ഭാഗം. മൂന്ന് വഴി:
  - **Google Gemini text (UPI works)** — **ഇത് മാത്രമേ UPI വഴി
    പണം അടയ്ക്കാൻ പറ്റൂ.** Gemini image-ന്റെ അതേ key മതി
    (checkbox ടിക്ക് ചെയ്താൽ വീണ്ടും ഒട്ടിക്കേണ്ട)
  - **Anthropic Claude** — വിദേശ card വേണം
  - **OpenAI GPT** — വിദേശ card വേണം
- **ഫലം വരുമ്പോൾ ഒരു ജാലകം വരും.** അതിൽ:
  - **മൂന്ന് തലക്കെട്ടാണെങ്കിൽ മൂന്നും കാണിക്കും**, ഇഷ്ടമുള്ളത്
    തിരഞ്ഞെടുക്കാം
  - **എഴുത്ത് അവിടെ വെച്ചുതന്നെ തിരുത്താം**
  - **Copy** / **Replace Frame Text** / **Insert as New Frame**
  - **Regenerate** — വീണ്ടും ചോദിക്കാം (**വീണ്ടും പണം ചെലവാകും**)
  - അടിയിൽ **ഏത് model, എത്ര token** എന്ന് കാണിക്കും
- **AI എഴുതിയത് താനെ page-ൽ വരില്ല.** ആ ജാലകത്തിൽ **നിങ്ങൾ
  തിരഞ്ഞെടുത്താൽ മാത്രമേ** frame-ൽ എത്തൂ. Frame-ൽ എഴുത്ത്
  ഉണ്ടെങ്കിൽ **മാറ്റണോ എന്ന് ഒരിക്കൽ കൂടി ചോദിക്കും**
- **Undo ചെയ്യാം** — "Summary", "Translation" എന്നിങ്ങനെ
  പേരോടുകൂടി
- **⚠️ ശ്രദ്ധിക്കുക:**
  - **നിങ്ങളുടെ എഴുത്ത് — അടിക്കുറിപ്പിന് ചിത്രവും — തിരഞ്ഞെടുത്ത
    കമ്പനിയുടെ server-ലേക്ക് പോകും**
  - Key `scribus172.rc`-ൽ **base64** ആയി സൂക്ഷിക്കും.
    **ആ ഫയലിനെ key പോലെ കരുതുക**
  - **ഇതുവരെ ശരിക്കുള്ള ഒരു key വെച്ചും ഒന്നും ചെയ്തുനോക്കിയിട്ടില്ല.**
    Claude-നും OpenAI-ക്കും ഇവിടെ account ഇല്ല; Gemini-ക്ക് ഉണ്ടെങ്കിലും
    അതിന്റെ rate limit പ്രശ്നം ഇനിയും ബാക്കിയാണ്

### 28. Keyboard Shortcuts — പേരുള്ള Shortcut Sets (പുതിയത്)

- `File › Preferences › Keyboard Shortcuts`-ലെ **Loadable Shortcut Sets**
  എന്ന **ഒറ്റ list**-ൽ എല്ലാ sets-ഉം:
  - "Scribus Default", "Newspaper Default", Scribus-ന്റെ സ്വന്തം sets
    (iCalamus, Photoshop Style, Scribus 1.7.0 …)
  - പിന്നെ ഒരു വരയ്ക്ക് താഴെ **നിങ്ങളുടെ sets**, പേരിനൊപ്പം **[User]**
  - Default set-ന് ശേഷം **"(Default)"** — ഉദാ. "My Layout [User] (Default)"
  - List-ൽ ഒരു set തിരഞ്ഞെടുത്താൽ (അല്ലെങ്കിൽ **Load**) അത് മുകളിലെ
    list-ൽ വരും; **OK** അമർത്തിയാൽ menus-ൽ
- Load-ന്റെ അടുത്ത് നാല് buttons:
  - **Save As…** — പുതിയ പേരിൽ സൂക്ഷിക്കും. പേര് നേരത്തേ ഉണ്ടെങ്കിൽ
    **മാറ്റണോ എന്ന് ചോദിക്കും**
  - **Save** — തിരഞ്ഞെടുത്ത set-ൽ ഇപ്പോഴത്തെ മാറ്റങ്ങൾ സൂക്ഷിക്കും
  - **Set as Default** — ആ set default ആക്കും, **ഉടനെ തന്നെ menus-ൽ
    വരും** (Cancel അമർത്തിയാലും)
  - **Delete** — ചോദിച്ച ശേഷം മായ്ക്കും. Default ആണ് മായ്ച്ചതെങ്കിൽ
    **"Newspaper Default"** default ആകും
  - Built-in sets-ൽ **Save-ഉം Delete-ഉം മങ്ങിക്കിടക്കും**
- **Scribus തുറക്കുമ്പോഴെല്ലാം Default set ആണ് വരുന്നത്.**
  - Default **നിങ്ങളുടെ set** ആണെങ്കിൽ, shortcut മാറ്റി **OK**
    അമർത്തുമ്പോൾ ആ മാറ്റം **ആ set-ൽ തന്നെ സൂക്ഷിക്കും** — അടുത്ത തവണയും
    ഉണ്ടാകും
  - Default **built-in set** ആണെങ്കിൽ മാറ്റങ്ങൾ അടുത്ത തവണ **പോകും**.
    സ്വന്തം keys വേണമെങ്കിൽ Save As… ചെയ്ത് ആ set default ആക്കുക
  - ഒരിക്കലും Default തിരഞ്ഞെടുക്കാത്ത profile-ൽ അടച്ചപ്പോഴുള്ള
    shortcuts തന്നെ
- **എവിടെ സൂക്ഷിക്കുന്നു:** ഓരോ set-ഉം
  `~/.config/scribus/shortcut-sets/<പേര്>.xml`-ൽ. Default-ന്റെ പേര്
  `prefs172.xml`-ൽ (`default_set`)
- പഴയ `my-default-shortcuts.xml` ആദ്യ തവണ `shortcut-sets/My Default.xml`
  ആയി മാറും
- **Export… / Import…** — set ഒരു `.xml` ഫയലാക്കി മറ്റൊരു
  കമ്പ്യൂട്ടറിലേക്ക്. പഴയ **Reset** ഇപ്പോൾ **Reset to Scribus Defaults**
- **പുതിയ profile-ൽ ആദ്യം:** `/usr/local/share/scribus/default-shortcuts.xml`
  ഉണ്ടെങ്കിൽ ചോദിക്കാതെ അത്; ഇല്ലെങ്കിൽ "Welcome" dialog — അതിലെ
  തിരഞ്ഞെടുപ്പ് ആദ്യത്തെ Default set ആകും
- **"Newspaper Default" ഇപ്പോൾ ഓഫീസിലെ "dbi" set തന്നെയാണ്** (393
  actions). ഇത് `resources/keysets/malayalam-dtp.xml`-ൽ നിന്ന് വരുന്നു,
  `.deb`-ൽ `/usr/local/share/scribus/keysets/malayalam-dtp.xml` ആയി
  പോകും. Shortcuts മാറ്റിയ ശേഷം പുതിയത് Newspaper Default ആക്കാൻ:
  ആ set **Set as Default** ആക്കി,
  `tools/update-newspaper-shortcuts.sh --deb` ഓടിക്കുക (keyset
  പുതുക്കി, build, install, `.deb` ഉണ്ടാക്കും)
- **Styled Copy / Styled Paste**-ന് set-ൽ കൊടുത്ത keys തന്നെ നിൽക്കും
  (ഉദാ. Ctrl+Shift+M). Set-ൽ key ഇല്ലെങ്കിൽ മാത്രം Ctrl+Shift+C /
  Ctrl+Shift+V
- **⚠️ ശ്രദ്ധിക്കുക:** Set-ന്റെ പേരിൽ മലയാളം ആകാം, പക്ഷേ ഫയലിന്റെ
  പേരിൽ ആ അക്ഷരങ്ങൾ `_` ആയി മാറും

### 29. Paragraph Styles panel — ഏത് style-നും keyboard shortcut (പുതിയത്)

- Panel-ലെ style-ൽ **right-click › Assign Shortcut…**, അല്ലെങ്കിൽ താഴെ
  ✎-ന്റെ അടുത്തുള്ള **⌨ button**. ചെറിയ dialog: key അമർത്തുക → OK.
  **Clear** അമർത്തിയാൽ key മാറും
- Key സൂക്ഷിക്കുന്നത് Scribus-ന്റെ സ്വന്തം style shortcut field-ൽ ആണ്
  (`Edit › Styles`-ലെ Shortcut tab കാണിക്കുന്നത് ഇതേ key). അതുകൊണ്ട്
  **.sla-യിലും template-ലും കൂടെ പോകും**
- ഓരോ row-ന്റെയും വലത്ത് അതിന്റെ key. Next Style Chain-ഉം സ്വന്തം key-ഉം
  രണ്ടും ഉണ്ടെങ്കിൽ: **⌨ Ctrl+1** (style) + **Ctrl+Shift+1 🔗** (chain)
- Key അമർത്തിയാൽ: text edit ചെയ്യുമ്പോൾ cursor ഉള്ള paragraph-ന്
  (അല്ലെങ്കിൽ select ചെയ്ത എല്ലാ paragraph-കൾക്കും); frame മാത്രം select
  ചെയ്തിട്ടുണ്ടെങ്കിൽ frame മുഴുവനും. Ctrl+Z-ൽ undo ആകും
- **Conflict warning:** ആ key വേറൊരു style, chain, column config, അല്ലെങ്കിൽ
  ഇപ്പോഴത്തെ shortcut set-ലെ (Newspaper Default ഉൾപ്പെടെ) ഒരു action
  ഉപയോഗിക്കുന്നുണ്ടെങ്കിൽ ആരാണെന്ന് കാണിക്കും — **Replace / Cancel**.
  ⚠️ Menu action-ന്റെ key Replace ചെയ്താൽ അത് **ഈ session-ൽ മാത്രം**;
  അടുത്ത തവണ Default set വീണ്ടും വരും. സ്ഥിരമായി മാറ്റാൻ
  `Preferences › Keyboard Shortcuts`-ൽ set മാറ്റി Save ചെയ്യുക
- ⚙ menu: **Export style shortcuts… / Import style shortcuts…** — style-ന്റെ
  പേര് വച്ച് ഒരു JSON file. വേറെ document/template-ൽ import ചെയ്താൽ ആ
  പേരിലുള്ള styles-ന് key കിട്ടും; ഇല്ലാത്ത പേരുകൾ report-ൽ കാണിക്കും
- Character Styles-ന് ഇത് ഇല്ല (അങ്ങനെ ഒരു panel ഇല്ല)

### 30. Column Style config → Design Style (പുതിയത്)

- `Design Style › ⚙ Settings › Column Style`-ൽ ഓരോ config-ന്റെ Add/Edit
  dialog-ൽ **Design Style** dropdown (None + എല്ലാ Design Style പേരുകളും).
  Default **None** — പഴയ configs പഴയതുപോലെ തന്നെ
- Config-ന്റെ key (Ctrl+Alt+1 … 8) അമർത്തിയാൽ: ആദ്യം columns, പിന്നെ
  അതേ frame-ൽ ആ Design Style — icon-ൽ click ചെയ്തതുപോലെ. രണ്ടും കൂടി
  **ഒരു Ctrl+Z**-ൽ തിരിച്ചു പോകും (columns, frame width, styles, auto-fit,
  design ഉണ്ടാക്കിയ image/caption frames എല്ലാം)
- List-ൽ: `Config 1 | Styles: [] | Split: | Cols: 1 | Design: style-1 | Key: Ctrl+Alt+1`
- Link ചെയ്ത Design Style ഇല്ലെങ്കിൽ (പേര് മാറ്റി / delete ചെയ്തു): columns
  വരും, status bar-ൽ **"Design style 'style-3' not found"**
- Design Style tab-ൽ പേര് മാറ്റി **Save** ചെയ്താൽ links കൂടെ മാറും; Remove
  ചെയ്താൽ ആ links **None** ആകും
- **Link Config N → style-N** button: `style-1` … `style-8` (അല്ലെങ്കിൽ
  "Style 1", "1") എന്ന പേരുള്ള Design Styles ഉള്ള configs-ന് ഒറ്റയടിക്ക് link.
  Match ഇല്ലാത്തവ അതുപോലെ തന്നെ
- **Export Column / Import Column**-ൽ link കൂടെ പോകും; പഴയ export file
  import ചെയ്താൽ എല്ലാം None
- Settings › Save-ന് ശേഷം design icons click ചെയ്താൽ ഒന്നും ചെയ്യാത്തത്
  (restart വരെ) ശരിയാക്കി

### 32. Linked images: badges, Embed All, check before output (പുതിയത്, 2026-10-02)

- **Badges on the canvas** (screen only, never in PDF / print / image export / preview mode):
  orange **LINK** tab = image is only linked, red **MISSING** tab = file missing. White outline, so they
  show on dark and light photos. Same size on screen at every zoom; a frame too narrow for the word
  gets a plain orange square / red square with a white cross. Sits 8 px inside the top-left corner,
  clear of the resize handles.
  Size: Preferences > Item Tools > "Linked / missing image badge size" — Small (20 px) / Medium (24 px) /
  Large (28 px, default). Applies at once, no restart.
  Toggle: View > Image Frames > Show Linked Image Badges (also in the page right-click menu). On by default.
- **Extras > Embed All Images** (also page right-click menu, and the control bar's "Embed in SLA"):
  embeds every linked image, one Undo takes all back, reports how many and which files were missing.
- **Before Save / Save As / Save as PDF / Print / Proof Print**: if any image is linked or missing, a list
  (page, frame, file, status) with "Embed all and continue", "Continue anyway", "Cancel", and
  "Don't ask again for this document" (stored in that .sla only; turn back on with
  Extras > Warn About Linked Images in This Document).
- **Preflight Verifier**: new problem "Linked (not embedded) image" (Preferences > Preflight Verifier >
  "Check for linked (not embedded) images", on by default). On its own it does not interrupt print/export.
- **Preferences > Item Tools > "Always embed placed images"** (on by default): Get Image, Ctrl+I,
  double-click load, drag and drop, News Browser images and the control-bar image tools embed automatically.

### 33. Network drive not reachable — Scribus no longer hangs
- If the office share (drive F) does not answer, Scribus starts anyway: each folder from the
  preferences is asked in the background and given 2 seconds. A folder that does not answer is
  skipped for this session and the status bar shows `Network folder not reachable: <path> skipped`.
- Skipped, never forgotten: recent documents, scrapbooks and file-dialog bookmarks on the share stay
  in the preferences and work again when the share is back.
- File dialogs (Open, Get Image / Ctrl+I, Save As ...) open in the local Documents folder when their
  usual folder is on the unreachable share, and leave the share out of the sidebar.
- Autosave skips a round (with a status-bar note) instead of freezing when its folder is unreachable.

### 34. Align and Distribute button on the control bar
- A button at the right end of the top control bar opens and closes the Align and Distribute palette
  (the same one as Windows > Align and Distribute). It is in the same place for every selection:
  image, text (frame selected or editing), line, shape, group, several items.
- Pressed while the palette is showing. Hidden when nothing is selected (the empty control bar stays
  empty) and back at the right end as soon as something is selected. An open palette is not closed
  when the selection is cleared; only the button hides.
- A floating palette opens just under the button, not over the page. A docked one is shown / raised.
- The palette keeps its Reference and Mode between opens.
- Narrow window: the button moves behind the bar's ">>" — click ">>" and the bar opens a second row with it.

### 35. PDF export presets and a Default that wins over the document
- **File > Export > Save as PDF** now starts with a **Preset** box: choose a preset and every tab is
  filled in at once. Buttons: Save As... (new name), Save (update the selected one), Delete (asks first),
  Set as Default. `(modified)` appears next to the name when you change something afterwards.
- **Use current settings as Default**: saves what is on screen and makes it the Default — into the
  selected preset if it is one of yours, otherwise into a preset called "My Default".
- **The Default wins.** Every time the dialog opens, for any document (new, old, from a template, from
  another PC), it shows your Default preset, not the PDF settings stored in the .sla. Tick
  **"Use this document's own saved settings instead"** for the rare case you want those.
- Exporting with a preset does **not** change the PDF settings saved in the document. They change only
  when you export with that box ticked (or when you use no preset at all).
- A preset holds everything on all tabs — version, image compression/quality/downsampling, font
  embedding, colour output and ICC profiles, PDF/X, marks and bleeds, viewer, security — plus "one file
  per page" and "open after export". It does **not** hold the file name or the page range.
  Per-font embed/subset lists are per document, so a preset keeps the embedding mode and
  "subset or embed fully", not the lists.
- Presets live in `~/.config/scribus/pdf-presets/` (one `.json` each; `default.txt` names the Default).
  **Export... / Import...** copy them to and from a folder for another PC.
- **Office presets**: tick "Office preset" on the ones to ship. `tools/release.sh` copies them into the
  package (read-only, shown as "(office)"), passwords left out; if your Default is one of them it becomes
  the office Default. An office user's own Default is in their own profile and an update never changes it.
- **File > Export > Save as PDF (Default preset)** — **Ctrl+Alt+Shift+D**: exports at once with the
  Default, asking only for the file name (all pages). **File > Export > Save as PDF with preset >**
  lists every preset and does the same with the one you pick.
- The old "News_Paper" and "Deshabhimani_Newspaper" entries are still there as "(built-in)".
- A preset that needs colour management (ICC profiles, PDF/X) on a document that has it switched off:
  the document's colour management is switched on when the export starts, not when the dialog opens,
  and stays on afterwards.

### 36. Very large images no longer crash Get Image; preview limit
- Clicking a huge plate TIFF (for example 40001 x 28801 pixels, 1-bit, 1270 dpi) in the Get Image /
  Ctrl+I dialog used to crash Scribus. It no longer does: an image that needs more than 2 GB of memory
  is refused at once and the frame stays empty, instead of crashing.
- The preview in the file dialog is not built for images above **100 megapixel**. The dialog shows
  "Too large to preview" with the size in pixels and megapixels. Smaller images preview as before.
  (The size is read from the file header for TIFF, PSD, JPEG, PNG and the other formats Qt reads.)

### 37. Duplicate News Checker (Extras > SR Tools)
- Replaces the old "Duplicate Content Check". Finds news printed more than once: the same
  **headline**, or the same **story text** (exact or nearly the same).
- A chain of linked frames is one story. The headline is the leading paragraphs in a headline style
  (size styles such as "16 M" / "72 B", kicker styles), else the leading paragraphs set clearly larger
  than the body, else the first paragraph. Bylines and datelines are never part of the headline.
- Before comparing, Malayalam is normalised: ZWJ/ZWNJ dropped, old chillus (ന്‍) and new chillus (ൻ)
  treated as the same, spaces, line breaks, hyphenation and punctuation ignored.
- **Similarity threshold** (default 85 %). A story's score is the share of its characters inside
  passages both stories have, so a story with a few words changed still scores in the 90s. A short
  story that is fully inside a longer one is reported as "(short story inside a longer one)".
- **Check:** current page / whole document / all open documents / all .sla files in a folder (for
  today's pages). Files in the folder are read straight from disk, without opening them; a file that
  is already open is checked as it is on screen, unsaved changes included.
- Captions, pull quotes and highlight boxes are left out by default (they repeat a sentence of their
  story), and so are frames on the pasteboard; a checkbox brings each back. One-word headlines and
  bodyless furniture (date line, masthead, "NEWS PAPER") are not compared as headlines.
- **Results:** each group is listed as "Headline duplicate" or "Content duplicate" with its %, and
  each item with page (and file), frame name and first words. Clicking an item goes to the page and
  selects the frame; an item in a folder file that is not open opens that file first.
- **On the page:** every frame of a group gets the group's colour (group 1 orange, 2 purple, 3 teal,
  4 blue, 5 green ...) as an outline, a light tint and a numbered badge, and the matching headline /
  passages are tinted inside the frame. Screen only: never in print, PDF or export, and not shown in
  Preview mode.
- **Clear highlights** / **Show highlights**. Highlights disappear when the window is closed unless
  **Keep highlights after closing** is ticked. Reopening the window shows the last results again.
- Right-click a group > **Ignore this headline/text from now on** for page furniture that repeats on
  every page (masthead, imprint). **Forget ignored** clears the list.
- Runs in the background with a progress bar and **Cancel**. 16 real pages read from a folder: 0.8 s.

### 38. Control bar for a selected group
- Selecting a **group** used to leave the top control bar empty. It now shows the toolbar of what is
  inside the group (nested groups included): only images -> image toolbar, only text frames -> text
  toolbar, only lines -> line toolbar, only shapes -> shape toolbar.
- **Mixed group** (image + caption text, as Design Style makes): a small **Edit:** dropdown at the
  start of the bar lists the kinds present with their counts (Images / Text / Lines / Shapes). It
  starts on the first kind found; the last kind picked is remembered until Scribus is closed.
- **Controls that act on the group itself:** text flow (none / shape / box / contour) and the wrap
  **Gap** buttons - news text flows around the whole group - plus flip, rotate 90, the W / H fields
  (group size), to front / up / down / to back, and Align and Distribute. "Image clip" flow is greyed
  out (a group has no clip path). The group's wrap gap is now saved in the .sla file.
- **Controls that act on every matching frame inside the group:** image rotation, fit frame / fit
  image, Auto-Fit, fill and line colour / opacity / width, corner radius; font, size, style, effects,
  alignment, colours, line spacing, tracking, scaling, indents, paragraph spacing, columns, frame
  line and fill, and the text "Gap" buttons when **Internal** is ticked (text inset). With Internal
  unticked those Gap buttons set the group's wrap distance.
- **One frame at a time:** Crop, Resize image (RS), DPI, Remove background, Draw / Edit / Auto contour
  and Edge feather work when the group holds exactly one image (or one text frame) - the image +
  caption case - and are greyed out when it holds several. Crop and the contour tools select that
  frame inside the group first, like Ctrl+click.
- Every change is **one** undo step ("Change Group Contents"); the group stays selected and grouped.
  After a change the bar shows the new values; a number or list that still differs between the
  children is shown **blank**. Colour swatches and on/off buttons show the first child's value.
- **Shortcuts and right-click on a group:** Adjust Frame to Image (Ctrl+Alt+I), Adjust Image to Frame
  (Ctrl+Alt+F), Adjust Frame Height to Text, Fit Caption Frame and Embed Image now act on the frames
  inside the group, and the group's outline follows frames that changed size. Embed Image on a group
  embeds every linked image in it (taking images back out stays a one-frame command).
- Also fixed on the way: the "all sides" Gap +/- buttons took two Ctrl+Z per click (now one, still
  1 mm per click); undo of To Front / To Back put an item taken from the middle at the wrong level;
  Bold / Italic on a font named "... Regular" now finds "... Bold".
- Entering the group and selecting one child shows that child's normal toolbar.

### 39. Duplicate-shortcut check only when wanted
- **Preferences > SR Menu > "Check for duplicate shortcuts when opening documents and at startup"**.
  Off by default, also on the office PCs after an update.
- **Off:** no "Duplicate shortcuts" popup when Scribus starts or a document or template is opened, and
  the check itself is not run, so opening costs no time for it.
- **On:** as before - the popup lists the keys assigned more than once, with "Don't show again until
  something changes".
- **Extras > Check Duplicate Shortcuts...** runs the check once whenever you want, whatever the
  checkbox says, and shows the same list (or says that nothing is assigned twice).
- Unchanged: the red "already used by..." warning while assigning a key (Keyboard Shortcuts, Assign
  Shortcut on styles, chains and column configs).

### 40. Text keeps its distance from rounded corners
- A text frame with rounded corners (one radius for all, or individual corners from the control bar)
  applied the text distances (inset) only on its straight sides; along the curve the text ran right
  up to the edge. The text area is now the frame's real outline moved inward by the text distances,
  so lines are shortened near a curve and keep the same gap there. Works with columns.
  A plain rectangular frame is laid out exactly as before.
- Changing a corner radius, or the inset with the control bar's **Internal** Gap buttons, now
  re-flows the text at once. Before, the old layout stayed until something else touched the frame.
- Text flowing **around** a rounded frame already followed the curve in "frame shape" mode; it now
  also updates at once when a corner radius changes, and the contour line follows the new outline
  (unless you edited the contour by hand).
- Text flowing around a **group** follows the rounded corners of a frame that sits at the group's
  edge, instead of the group's plain rectangle.
- Note: existing pages with rounded or other non-rectangular text frames that have an inset will
  re-flow slightly when opened, because the inset now applies along the curve too.

### 41. Fill Text with Image (image visible only inside the letters)
- **Item > Fill Text with Image...**, also in the right-click menu of a text frame and as the
  **▣T** button on the text control bar. Select one text frame, choose an image.
- The text becomes ONE image frame in the shape of the letters, as the page shows them (so Malayalam
  conjuncts and vowel signs are right), with the image scaled to cover the letters, proportions
  kept, centred. It is an ordinary image frame afterwards: double-click to move or scale the image
  inside the letters.
- Options in the dialog: a thin **outline** around the letters (width and colour) and a soft
  **drop shadow**. The choices are remembered.
- The original text frame is kept on the layer **"Original text"**, which is hidden and does not
  print. To change the words: show that layer, edit the text, select it and run the command again -
  the old image frame is replaced.
- One undo step. The frame must not be linked to other frames or inside a group.

### 42. Help > About shows when this build was made
- The About box said "Built: 13 April 2026" for every build (the date of the upstream release).
  It now shows the real date and time this copy was built, on the first tab under the version and
  on the Build Info tab - the same stamp as in the window title.

### 43. Text Effects: Bevel & Emboss (Photoshop-style headline)
- **Item > Text Effects...**, also in the right-click menu. Select ONE of: a Fill Text with Image
  result, a text frame, a shape, or a group of shapes (a headline converted to outlines).
- Styles: Inner Bevel, Outer Bevel, Emboss, Pillow Emboss, Deboss (Stamped). Settings: Technique
  (Smooth / Chisel Hard), Depth, Direction, Size, Soften, Light Angle, Altitude, Highlight and
  Shadow colour + opacity, Resolution (300 / 600 dpi), Output. Presets: Classic Emboss, Gold Bevel,
  Stamped, Pillow. The dialog has a small preview.
- **Bake (print-safe)** is the default: the highlight and the shadow are written into a copy of the
  image inside the letters. No transparency goes to the PDF. For Outer Bevel, Emboss and Pillow
  Emboss the part outside the letters sits in an opaque frame behind them, filled with the colour
  chosen for "Behind the letters" - pick the colour the headline stands on.
- **Live (transparency)** puts the highlight and the shadow in two frames (Screen / Multiply) over
  the letters. PDF/X-1a, PDF 1.3 and PostScript printing cannot carry that - use Bake for the press.
- A text frame, shape or group is kept on the hidden layer "Original text" and a letter-shaped image
  frame with a plain-colour image is made from it (text colour / fill colour; a CMYK colour gives a
  CMYK TIFF, so Black stays on the black plate).
- The images are saved in the folder **"<document>_effects"** next to the .sla, so the document
  must be saved first (the command asks). The original picture is never changed.
- Apply again replaces the old result. "Remove Effect" takes it away. One Apply = one undo step.
  Images that are no longer used are deleted when the document is closed.
- The generated images are ordinary linked images: the linked-images check, Embed All Images and
  Collect for Output see them.
- Very large headlines: the image is limited to 40 megapixels (the resolution is lowered, with a
  message).
- The Phase-1 effects (extrude, stroke, shadow, gradient) are not in this build; this dialog holds
  Bevel & Emboss only.

### 44. Edge Feather/Blur can be undone
- One **Ctrl+Z** takes back one Apply of Edge Feather (image frame, text frame "Fe" button, the
  feather popup for shapes / several frames / a group). **Ctrl+Shift+Z** brings it back. The Action
  History shows it as **Edge Feather/Blur**.
- A locked frame, or a frame on a locked layer, is not changed: a message says to unlock it first.
- The feathered picture files are not deleted at Apply (Undo needs them). The ones the saved
  document does not use are removed when the document is closed.
- Exporting a PDF no longer adds "Drop Shadow" steps to the Action History.
- **Remove Background** is the same: one Ctrl+Z takes back the picture, the contour and the text
  flow together ("Remove Background" in the Action History). It ends in contour editing, where Undo
  is greyed: press **OK** in the Nodes palette (or Esc) first, then Ctrl+Z. The **Auto Contour**
  button is one undo step too, and its contour is now really restored.

### 45. Design Style icons work on every PC
- Icon ഇനി file പേരുകൊണ്ട് സൂക്ഷിക്കുന്നു (`style-1.png`), മുഴുവൻ path അല്ല
- നോക്കുന്ന ക്രമം: `~/.local/share/scribus/design-icons/` → `/usr/local/share/scribus/design-icons/` → `/usr/share/scribus/design-icons/`
- പഴയ conf-ലെ ഇല്ലാത്ത path (`/home/s1/Documents/style-1.png`) പേരുകൊണ്ട് കണ്ടെത്തും — തിരുത്തേണ്ട
- 14 default icons .deb-ൽ തന്നെ ഉണ്ട്
- Upload Icon: file `~/.local/share/scribus/design-icons/`-ലേക്ക് copy ചെയ്യും; അതേ പേരിൽ വേറെ ചിത്രം ഉണ്ടെങ്കിൽ ചോദിക്കും
- Style Settings-ൽ icon കണ്ടെത്തിയ path കാണിക്കും
- Export Design icon ചിത്രങ്ങളും കൂടെ കൊണ്ടുപോകും; Import Design അവ തിരികെ ഇടും
- Icon കിട്ടിയില്ലെങ്കിൽ button-ൽ `?` കാണിക്കും
- Settings files ഇരിക്കുന്നത് `~/.config/Scribus/`-ൽ (`SuneerDesignStyle.conf`, `SuneerColumnConfig.conf`)

### 46. Old Scribus pages keep their old line breaks
- Scribus 1.5/1.6-ൽ ഉണ്ടാക്കിയ page തുറന്നാൽ വാക്കുകൾ പഴയ സ്ഥലത്ത് തന്നെ മുറിയും — fit ആക്കിയ frames overflow ആകുന്നത് കുറയും
- കാരണം നമ്മുടെ build-ലെ രണ്ട് മാറ്റങ്ങളായിരുന്നു (Qt/font അല്ല)
- Save ചെയ്താൽ ഈ അടയാളം file-ൽ സൂക്ഷിക്കും
- നമ്മുടെ പഴയ build-ൽ ഇതിനകം save ചെയ്ത പഴയ page-ന്: **Extras → Old Scribus Line Breaks (This Document)** tick ചെയ്യുക
- ഈ build-ൽ ഉണ്ടാക്കിയ pages മാറില്ല
- പഴയ layout 100% അല്ല: ചില frames ഇപ്പോഴും overflow ചെയ്യാം

### 47. Condense to Fit (frame വലുപ്പം മാറ്റാതെ)
- Overflow ചെയ്യുന്ന text-ന്റെ അക്ഷര വീതി (horizontal scale) 0.5% വീതം കുറച്ച് fit ആക്കും; frame-ന്റെ വലുപ്പവും സ്ഥാനവും മാറില്ല
- Story മുഴുവനും ഒരുപോലെ ചുരുങ്ങും; overflow ഇല്ലാത്ത frames തൊടില്ല
- പഴയ file തുറക്കുമ്പോൾ വരുന്ന **Legacy Document** ചോദ്യത്തിൽ "Condense text to fit - don't resize the frame" (default tick)
- ഏത് page-ലും: **Item → Condense to Fit (Keep Frame Size)**, right-click menu, control bar-ലെ `→T←`
- പരിധി: **Preferences → SR Menu** — scale 90%, tracking −2% (മാറ്റാം)
- Fit ആകാത്തവ പഴയപടി വെച്ച് പേര് കാണിക്കും (**Select Them**); locked frame / locked layer തൊടില്ല
- ഒരു Ctrl+Z കൊണ്ട് എല്ലാം തിരികെ ("Condense to fit")

### 48. Frame Border: rounded corners
- Control bar-ലെ **☐ Frame Border** popup-ൽ **Corner radius** (mm) + നാല് corner checkboxes
- തിരഞ്ഞെടുത്ത രണ്ട് വശങ്ങൾ ചേരുന്ന corner മാത്രം വളയും; തുറന്ന വശത്ത് വര നേരെ അവസാനിക്കും
- Border ഒറ്റ വരയായി (വര → വളവ് → വര): PDF-ലും print-ലും join വൃത്തിയായി
- Text വളവിൽ തൊടില്ല
- Frame resize ചെയ്താൽ border കൂടെ വരും; popup വീണ്ടും തുറന്നാൽ ഇപ്പോഴത്തെ മൂല്യങ്ങൾ
- ഒരു Apply = ഒരു Ctrl+Z; പല frames ഒരുമിച്ച്
- പഴയ pages-ലെ border മാറില്ല

### 49. Authors
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

### 50. Poster Stack (Item > Poster Stack...)
- Stretches each line of a text frame to the full frame width and stacks the lines to fill its
  height, with one image showing through all the letters (typographic poster). Live preview.
- Options: multi-line text with auto split (characters per line / balanced), font, letter gap,
  line gap, outer margin, stretch or keep proportions, equal or proportional row heights, image,
  background colour. Malayalam splits only at grapheme clusters.
- Result is a Fill Text with Image frame: move/zoom the image afterwards, Text Effects work on it.
  Settings are kept on the frame; reopening the dialog edits the poster, re-apply replaces it.
- Frame Border popup: live preview while choosing sides, inset, radius and corners; Preview
  checkbox (remembered); Cancel/Esc restores the frame exactly; Apply is one undo step.
- Printing: the job goes to lpr, or to lp when lpr is not installed, without a shell; a refused job
  reports the queue's real state (missing, disabled, not accepting) or says to install cups-bsd.
  The .deb depends on cups-bsd and cups-client. The proof dialog forgets a printer name that no
  longer exists and saves the default instead.
- Pop-out Subject (Item > Pop-out Subject..., also from Poster Stack): the head of the person in the
  picture comes out in front of the letters, the rest stays inside them. rembg finds the person; the
  mask becomes a vector clip (no soft mask in the PDF). Head only / above a draggable line / custom
  rectangle or ellipse. The pop-out follows the picture when it is moved or zoomed inside the letters.

### 51. News Browser — പുതിയ workflow API, login, server-ൽ "placed" state, reference layout
- Server-ൽ `/api/v1/external/*`-ൽ `editions`-ഉം `layout-templates`-ഉം ഇല്ലാതായി; ഇപ്പോൾ editions
  public `initial-data/static`-ൽ നിന്ന് (user-ന്റെ editionGroups-ൽ ഉള്ളവ മാത്രം), pages
  `/external/edition-pages/<SHORT NAME>` (displayOrder), news `/external/edition-pages/<pageId>/news`.
- Settings (⚙): API server, Files base URL (default `http://<host>/files`), edition short-name fallback,
  Layout (default photo position, text-wrap gap 2 mm, pasteboard left/right/page), Style tab
  (title/kicker/highlights/byline/dateline/first-body/body/caption). പഴയ `apiUrl` തനിയെ migrate; settings password ഇല്ല.
- **Login** (email + password; password ഒരിടത്തും store ചെയ്യുന്നില്ല). Token-ഉം refresh cookie-യും deviceId-ഉം
  system keychain-ൽ (QtKeychain, service `org.scribus.news`). Expired → refresh ഒരു തവണ → login dialog.
  Startup-ൽ silent refresh. Logout button. Login ഇല്ലാതെ panel locked.
- List: title മാത്രം (Malayalam conjunct മുറിയാതെ "…"), tooltip-ൽ kicker/title/byline/photo, photo icon + count.
- **Placed state server-ൽ** (`PATCH /news/dtp-status`): place ചെയ്താൽ USED; USED stories grey ✓ + disabled;
  Check button; "Mark as unused" (list / right-click); page-ൽ right-click "Remove story (mark unused)" —
  frames പോകും, server BALANCED; Undo → frames തിരിച്ച് + USED. Delete key = സാധാരണ delete (hint മാത്രം).
- **Placement** (reference `sambile.sla` പോലെ): ഒരു text frame (kicker, headline, dateline, body — headline
  style-ന്റെ SpanColumns), photo വേറെ frame (N columns, aspect ratio) + welded caption (`caption_<image>`, 09 Caption),
  text wrap offsets 2 mm (live reflow). Group ഇല്ല; items story id attribute-ൽ; right-click "Select whole story",
  "Photo position ▸" (Top / Top above / Right / Left / Middle / Bottom), "Photo columns ▸".
- Place Selected → pasteboard-ൽ page-ന്റെ ഇടത്ത് story spike (stack, overlap ഇല്ല, pasteboard തനിയെ വീതി കൂടും,
  ഒരു undo step). Photos `~/.cache/scribus/news/`-ൽ (300 ppi working copy, original-ഉം), normal preview resolution.
- Commit `04c6eee` (feature/ctp-output).

### 52. "Grow the outline stroke" save/reopen + PDF
- Text outline "outward" option save + reopen-ൽ OFF ആകുമായിരുന്നു — 1.7.1 file format-ൽ attribute ഇല്ലായിരുന്നു.
  ഇപ്പോൾ character style, paragraph style, selected characters — എല്ലാ level-ലും save/load (`TextOutlineOutward`).
  Copy/paste, Scrapbook, Import styles ഇതേ വഴി. പഴയ files മാറ്റമില്ല (attribute ഇല്ലെങ്കിൽ off).
- Style Manager → Character → Outline popup-ൽ "Grow outward" checkbox (control bar checkbox-ഉമായി sync).
- **PDF bug**: embedded font-ൽ outward ignore ചെയ്തിരുന്നു; ഇപ്പോൾ PDF screen-ലെ പോലെ (stroke, പിന്നെ fill മുകളിൽ).
- Test: `tests/suneer/roundtrip_outline_outward.sh <file.sla>`. Commit `e27845e`.

### 53. Pop-out Subject — live preview dialog, mask brush; Poster Stack / Fill Text crash fix
- Pop-out Subject dialog-ൽ ഇപ്പോൾ **live preview** (paper, അക്ഷരങ്ങളിലെ picture, മുകളിൽ pop-out).
  Preview-ൽ picture നീക്കിയാലും zoom ചെയ്താലും അതേ placement face frame-ലും വരും.
- rembg-യുടെ mask **brush** കൊണ്ട് തിരുത്താം (കൂട്ടാം/കുറയ്ക്കാം); തിരുത്തിയ mask copy ആയി cache-ൽ.
- Poster Stack / Fill Text with Image fail ആകുമ്പോൾ ഉണ്ടായിരുന്ന crash (canvas redraw) മാറി:
  വാക്കുകൾ തിരിച്ചിടുന്നു, transaction cancel; Poster Stack-ന്റെ പുതിയ layer-ന് "Send to Layer" ഉടൻ.
- Commit `0127cae`.

### 54. File > Save As Old Version...
- Scribus 1.7.3 (office deb-3), 1.6.x, 1.5.x — ഇതിലൊന്നിന് തുറക്കാവുന്ന **copy** എഴുതുന്നു
  (`<name>-1.7.3.sla` / `-1.6.sla` / `-1.5.sla`). തുറന്നിരിക്കുന്ന document അതേപടി.
- 1.5/1.6 copy-യിൽ പഴയ Scribus-ൽ എന്ത് മാറും എന്ന് dialog-ൽ പറയും (text engine വേറെ, rules/shading
  കാണില്ല, spanning headline office 1.5.6-ൽ മാത്രം). Office 1.5.6 വായിക്കുന്ന `FullSpan`/`nxtStyle`-ഉം എഴുതുന്നു.
- Commit `e077200`.

### 55. Context menu — "Send to Layer" crash fix
- Code വഴി ഉണ്ടാക്കിയ layer-ന് action ഇല്ലാതിരുന്നാൽ right-click crash ആയിരുന്നു; ഇപ്പോൾ menu തുറക്കും മുമ്പ്
  layer list rebuild. Commit `17fd5f0`.

### 56. Update server: Debian server port 8095, tools/publish-update.sh
- Office PC-കൾക്കുള്ള updates ഇപ്പോൾ newsroom-ന്റെ Debian server-ൽ നിന്ന്: `http://<server>:8095/scribus-updates/latest.json`
  (host nginx, port 8095; ports 80/443-ലെ docker containers workflow-ന്റേത് — തൊടരുത്). Address repo-യിൽ ഇല്ല
  (`~/scribus-keys/release.conf`, build cache, `InstallScribus/install.sh`).
- `tools/publish-update.sh -m "..."`: build + sign (laptop-ൽ മാത്രം) → rsync (.deb, .sha256, .sig, latest.json അവസാനം) →
  server-ൽ last 3 → HTTP read-back (signature + sha256) → git tag. `--list`, `--rollback <version>`.
- Updater: server off/unreachable → "Update server X did not answer within 15 seconds…" (hang ഇല്ല). curl/wget വേണ്ട.
- Office PC ആദ്യ തവണ: പുതിയ .deb `InstallScribus/`-ൽ `sudo ./install.sh` (update.conf url ശരിയാക്കും); പിന്നെ Help > Check for Updates.
- Runbook: `~/Desktop/claude/runbook-20261008-scribus-update-server.md`. Commit `f901367`.

### 57. Help > Check for Updates — തെറ്റായ saved address, Settings dialog, messages
- Update Settings-ൽ save ചെയ്ത തെറ്റായ address (`//host/...`, `curl -sS http://…` പോലുള്ള paste, പഴയ server
  `scribus-updates.local` / port 8081 / port 80 `/scribus-updates`) ഇനി updates തടയില്ല: ഒഴിവാക്കി, മായ്ച്ച്,
  `/etc/scribus/update.conf`-ലെ address ഉപയോഗിക്കും (dialog-ൽ കാരണം കാണിക്കും).
- Settings: ഒരു http(s) address മാത്രം (തെറ്റെങ്കിൽ ചുവന്ന വരി, save ഇല്ല); **Use default** button; Enter = Check.
- Settings dialog ഒറ്റ instance, Check for Updates-ന്റെ മുകളിൽ centre-ൽ ഓരോ തവണയും.
- Saved address fail ആയാൽ **Try the default server** button. Open ചെയ്യുമ്പോൾ തന്നെ check; "You have the latest version (1.7.3-…)".
- Install-ന് മുമ്പ്: ആരുടെ password system prompt ചോദിക്കും എന്ന് (sudo group-ൽ ഇല്ലെങ്കിൽ ADMINISTRATOR (root) password).
- Signature + sha256 verify, pkexec install — മാറ്റമില്ല. Commit `3494742`.

### 58. News Browser — ticked used stories → "Mark unused (N)"
- Used (grey ✓) stories വീണ്ടും tick ചെയ്യാം — release ചെയ്യാൻ മാത്രം; place ചെയ്യില്ല. **Select all used** checkbox.
- Buttons tick അനുസരിച്ച്: "Place Selected (N)" free മാത്രം (used ticks skip — status-ൽ "N used story(ies) skipped");
  "Mark unused (N)" used ticks ഉണ്ടെങ്കിൽ (Place Selected-ന്റെ അടുത്ത്).
- Mark unused (N): ഒറ്റ confirmation (headlines; document-ൽ ഉള്ളവയ്ക്ക് "Also remove these N stories from the page" — default on,
  ഒരു undo step). ഓരോ story-ക്കും single "Mark as unused"-ന്റെ അതേ server call. Success → row free; fail → row used,
  അവസാനം summary-ൽ server-ന്റെ reason. മാറിയ rows മാത്രം refresh (re-fetch ഇല്ല).
- Right-click "Mark as unused" / "Remove story (mark unused)" പഴയപടി. Commit `3494742`.

### 59. News Browser — Free / Used / Balance status, Mark unused = UNUSED, Mark balance
- Server-ലെ status (`UNUSED` / `USED` / `BALANCED`) list-ൽ: Free (plain), Used (grey, ✓ icon; tooltip-ൽ page/time/user ഉണ്ടെങ്കിൽ),
  Balance (bookmark icon, amber — പിന്നീടേക്ക് മാറ്റിവെച്ചത്). മൂന്നും tick ചെയ്യാം.
- **Mark unused** ഇപ്പോൾ server-ൽ UNUSED (story വീണ്ടും free). ഇതിന് backend-ൽ ഒരു patch വേണ്ടി വന്നു
  (`/news/dtp-status` UNUSED സ്വീകരിക്കാൻ; 2026-10-08 deploy). പഴയ backend ആണെങ്കിൽ 400 → summary-ൽ reason.
- **Mark balance (N)** — Place Selected-ന്റെ താഴെ Mark unused-ന്റെ അടുത്ത്; list right-click "Mark as balance (keep for later)";
  page-ൽ story right-click "Mark as balance on server (keep frames)". Confirmation-ൽ headlines, "[in this document]",
  "Also remove … from the page (one undo step)".
- Buttons tick അനുസരിച്ച്: Place Selected (free+balance), Mark unused (used+balance), Mark balance (free+used).
- Balance story ticked ആയി Place Selected → ഒരു തവണ ചോദ്യം ("… BALANCE stories … Place them now? They will be marked USED") → place → USED.
- ഓരോ story-ക്കും result; server refuse ചെയ്താൽ reason summary-ൽ; മാറിയ rows മാത്രം refresh. Commit `5664da8`.

### 60. News Browser — status change-ന് മുമ്പ് server-ലെ ഇപ്പോഴത്തെ status (fromStatus), Place → USED ഉറപ്പ്, retry, undo
- Server-ന്റെ `/news/dtp-status` ഇപ്പോൾ `fromStatus` ചോദിക്കുന്നു. എല്ലാ status മാറ്റങ്ങളും (Mark unused, Mark balance, Remove story,
  Place → used, Undo) മാറ്റുന്നതിന് തൊട്ടുമുമ്പ് story-യുടെ ഇപ്പോഴത്തെ status server-ൽ നിന്ന് വായിച്ച് അത് fromStatus ആയി അയയ്ക്കുന്നു.
  മറ്റൊരു PC മാറ്റിയിട്ടുണ്ടെങ്കിൽ: "this story was changed by X - now STATUS" — row refresh, retry ഇല്ല.
- Buttons / right-click: ഇപ്പോഴത്തെ status-ൽ നിന്ന് പറ്റുന്ന actions മാത്രം (disabled ആയാൽ tooltip-ൽ കാരണം).
- Place Selected / double-click: ഓരോ story-യും വെവ്വേറെ USED. Fail ആയാൽ summary "Placed N stories; M could not be marked used…
  <headline> - <reason>", row "Placed here - not marked on server" (orange), right-click **Retry mark used**, Check-ഉം retry ചെയ്യും.
  Success ആയ row ഉടൻ Used (✓).
- Undo of Place Selected: frames പോകും + server-ൽ പഴയ status (fromStatus USED); fail ആയാൽ warning.
- Request/response log (token ഇല്ല): `~/.cache/scribus/news/api.log`. Commit `dca3488`.

### 61. Design Style apply — കൈകൊണ്ട് മാറ്റിയ size/spacing clear
- Design Style apply ചെയ്യുമ്പോൾ paragraph style reference ആയി ഇടുന്നു → paragraph-ൽ കൈകൊണ്ട് മാറ്റിയ line spacing (+mode), alignment,
  indents, space before/after, drop cap എല്ലാം style-ലേക്ക് തിരികെ. Character runs-ൽ font size, horizontal/vertical scale, tracking,
  word tracking, baseline offset, മറ്റൊരു family font — clear. **നിലനിൽക്കുന്നത്**: bold/italic (same family), colour/shade, underline,
  strikethrough, outline, shadow, super/subscript, caps, language, named character style.
- ഒറ്റ undo. Frame select → whole story (linked frames-ഉം); edit mode-ൽ text selection → ആ paragraphs മാത്രം.
- Preferences → SR Menu → "Clear manual size/spacing when applying a Design Style" (default ON). Icon **Shift+click** → പഴയപടി (manual values നിലനിർത്തും).
- Column Style → Design Style link, Design Style shortcuts: ഇതേ behaviour. Paragraph-style shortcuts വേറെ path (അവിടെ എല്ലാ char formatting-ഉം പോകും — മാറ്റിയിട്ടില്ല).
- Commit `d3fc7bc`.

### 62. Control bar Fill / Line popup — document colours, multi-selection
- Fill / Line button-ന്റെ popup (swatch list + Shade) ഇപ്പോൾ എപ്പോഴും **തുറന്നിരിക്കുന്ന document-ന്റെ** colour list: document open/switch,
  Edit > Colours, import, ഓരോ തവണ തുറക്കുമ്പോഴും refresh. പഴയ default set (None/Black/Registration/White) ഇനി കാണില്ല.
- പല frames select ചെയ്താൽ: fill colour, shade, line colour, shade, opacity — **എല്ലാ** selected frames-ലും (Properties പോലെ), ഒരു undo.
  Values വ്യത്യസ്തമെങ്കിൽ swatch blank + tooltip "Mixed …".
- ഇതേ "ആദ്യ frame മാത്രം" bug ഉണ്ടായിരുന്നവ ശരിയാക്കി: Columns, Column gap, Line style/cap/join/dash/arrows, image line style, W/H.
  (Gap, line width, text inset, corner radius, wrap, Auto-Fit — മുമ്പേ ശരി.)
- Note: popup-ൽ colour click ഒരു undo, shade മാറ്റം വേറെ ഒരു undo (Properties-ലും അങ്ങനെ). Commit `88910af`.

### 63. News Browser list — USD / UNUSD / BAL tag, status counts
- ഓരോ row-യുടെയും വലത്തെ അറ്റത്ത് ചെറിയ bold tag: **USD** (red) placed, **UNUSD** (green) free, **BAL** (orange) balance.
  Panel ഇടുങ്ങിയാൽ തലക്കെട്ട് മാത്രം "…" ആകും; tag എപ്പോഴും മുഴുവൻ കാണാം. List-ന് പുറത്ത് ഒന്നും വരയ്ക്കില്ല.
- Fetch കഴിഞ്ഞ് status line: "N stories - X USD, Y UNUSD, Z BAL". Client ഒരു story-യും hide ചെയ്യുന്നില്ല — server തരുന്നത് മുഴുവൻ.
- Used/balance rows സാധാരണ text colour (tag status പറയും); icons, checkbox, tooltip, Select All — പഴയപടി. Place / Mark unused / Mark balance / Check
  കഴിഞ്ഞ് ആ row-യുടെ tag ഉടൻ മാറും. Light/dark theme രണ്ടിലും colours.
- Commit `7dd888d`.

### 64. News Browser — used / balance stories list-ൽ, Show: filter
- Server-ന്റെ public route (`/external/edition-pages/<page>/news`) unplaced stories മാത്രമേ തരൂ. ഇപ്പോൾ Fetch web app-ന്റെ "Used News" / "Balanced News"
  page-കൾ ഉപയോഗിക്കുന്ന അതേ API (`/news/used`, `/news/balanced`, login-ന്റെ edition group id) + `/news/layout` കൂടി വിളിച്ച് story id കൊണ്ട് merge.
  Used story-ക്ക് USD, page name/reporter tooltip-ൽ; balance-only story place ചെയ്യുമ്പോൾ full text ആദ്യം server-ൽ നിന്ന് എടുക്കും.
- **Show:** all / USD / UNUSD / BAL filter (പഴയപടി ഓർക്കും); hidden rows-ൽ Place/Mark ഒന്നും ചെയ്യില്ല.
- Status line page മുഴുവന്റെ കണക്ക്: "Unused N · Used N · Balance N (· showing: USD)"; filter എല്ലാം മറച്ചാൽ "No USD stories (N total) - change Show:";
  login ഇല്ലെങ്കിൽ "Used: log in to see used stories"; HTTP 401/404/network error-കൾ status line-ൽ (silent ഇല്ല). `api.log`-ൽ ഓരോ call-ന്റെയും
  HTTP code, count, reply field names (token/id ഇല്ല).
- Office PC-യിൽ API host `http://<server>/api` (port 80) ആയും ഉപയോഗിക്കാം. Commit `7251c12`.

### 65. Paste — pasted text അതേപടി ("(pasted)" styles, old line breaks)
- Target document-ൽ അതേ പേരിൽ **വ്യത്യസ്ത** paragraph/character style ഉണ്ടെങ്കിൽ pasted text ഇനി target-ന്റെ style silently എടുക്കില്ല:
  incoming style "**<name> (pasted)**" ആയി വരും (തുല്യ definition ഉള്ള copy ഉണ്ടെങ്കിൽ reuse). Target-ന്റെ styles ഒരിക്കലും മാറില്ല; definition തുല്യമെങ്കിൽ target style തന്നെ.
- 1.5/1.6-ൽ നിന്ന് വന്ന (Old Scribus Line Breaks) document-ൽ നിന്ന് copy ചെയ്ത text frame ആ behaviour frame-ൽ തന്നെ കൊണ്ടുപോകും
  (clipboard, രണ്ടാം window, Scrapbook) → പുതിയ document-ൽ paste ചെയ്താലും അതേ line breaks, overflow ഇല്ല. Extras → Old Scribus Line Breaks off ചെയ്താൽ
  ആ document-ലെ frame mark-കളും പോകും. Commit `aa3f7e0`.

### 66. Contour wrap image-നോടൊപ്പം — BG auto contour, DC contour resize-ൽ നഷ്ടപ്പെടില്ല
- BG/AC-യുടെ auto contour image-ന്റെ alpha-യിൽ നിന്ന് ഒരു തവണ trace ചെയ്ത് frame resize, image scale/offset, rotate, Adjust image to frame, picture മാറ്റം
  എന്നിവയിലെല്ലാം image-ന്റെ ഇപ്പോഴത്തെ സ്ഥാനത്തേക്ക് re-map ചെയ്യും; frame-ൽ clip. പഴയ file-കളിലെ BG contour-ഉം ആദ്യ മാറ്റത്തിൽ ഇത് ഏറ്റെടുക്കും.
- **DC** (കൈകൊണ്ട് വരച്ച) contour ഒരിക്കലും overwrite/reset ആകില്ല: resize-ൽ frame rectangle ആകുന്ന പഴയ bug (ClipEdited) ശരിയാക്കി; image-നൊപ്പം നീങ്ങും/scale ആകും;
  shrink ചെയ്താൽ frame-ന് അകത്തെ ഭാഗം മാത്രം text തള്ളും, enlarge-ൽ മുഴുവൻ തിരികെ. DC-യും ഒറ്റ undo ("Draw Contour").
- Right-click → **Regenerate contour from image** (DC contour-ന് പകരം alpha-യിൽ നിന്ന് വീണ്ടും). Text flow mode മാറില്ല. Commit `dea85a1`.

### 67. Edge Feather — live effect, cut-out edge, side option
- Image-ന്റെ 🌫 Feather ഇനി file എഴുതുന്ന tool അല്ല; **non-destructive effect**: popup-ൽ amount/side മാറ്റുമ്പോൾ canvas-ൽ live, Apply ഒറ്റ undo, SLA-യിൽ save,
  PDF/print ഒരേപോലെ. BG കഴിഞ്ഞ (transparent) image-ൽ ആളുടെ **outline** soft ആകും (All); Top/Bottom/Left/Right → subject-ന്റെ യഥാർത്ഥ edge-ൽ നിന്ന് ആ side മാത്രം.
  Opaque image-ൽ പഴയ frame-edge feather. Amount image px-ൽ store; cache ഉണ്ട്. Commit `198b415`.

### 68. BG remove — photo-യുടെ തിരഞ്ഞെടുത്ത ഭാഗത്ത്
- **Shape:** image frame-ന് മുകളിൽ rectangle/ellipse/polygon/bezier/freehand വരച്ച് frame + shape(കൾ) select → BG → mode (Only inside / Keep subject only),
  model, feather → rembg background-ൽ (progress, Cancel) → apply, shape-കൾ delete, ഒറ്റ undo.
- **Dialog** (frame മാത്രം select ചെയ്താൽ): preview (zoom/pan), Rect / Lasso, Add/Subtract, Whole / Only inside / Keep subject, Run, Erase / Restore brushes (size, softness),
  dialog-ൽ Undo/Redo, **Reset to original**, Apply.
- Original file തൊടില്ല; result `<name>_rembg.png`, `_rembg2…`; selection/mode/brush/raw cut frame attributes-ൽ (reopen → അതേ state); contour/feather/effects പഴയപടി.
  Commit `198b415`.

### 69. Image Size — toolbar bar, dialog, Optimize Images
- Toolbar: `[cm▾] W [⚭] H  DPI  Resamp. [quality] [sharpen%] [↑] Auto  pixels/size`. W/H = picture-യുടെ page-ലെ വലുപ്പം (cm/mm/in ഓർക്കും), DPI colour
  (red <150, orange <200, green 200–400, blue >400). Enter/focus-out-ൽ apply.
  - **Resamp. ON:** W/H മാറ്റിയാൽ dpi അതേപടി, pixels = size × dpi, picture resample, frame-ഉം ആ size; DPI മാറ്റിയാൽ size അതേപടി, pixels മാറും.
  - **Resamp. OFF:** pixels മാറില്ല, file ഇല്ല; DPI → size (6000 px @72 = 211.67 cm, 300 dpi → 50.80 cm); size → dpi.
  - Quality: Auto (ചെറുതാക്കുമ്പോൾ Bicubic Sharper, വലുതാക്കുമ്പോൾ Smoother) / Sharper / Smoother / Bicubic / Nearest; Sharpen % luminance-ൽ മാത്രം (alpha അല്ല).
    ↑ = Allow enlarge (default off; status bar warning). Auto = mouse resize-ന് ശേഷം ഈ dpi-യിലേക്ക് resample.
  - Typing ചെയ്യുമ്പോൾ status bar-ൽ live: result px, dpi, ~MB (orange ⚠ original-ൽ കൂടുതലായാൽ).
- എല്ലാ resample-ഉം **original file-ൽ നിന്ന്** (`SuneerImgOriginal` attribute), output `<doc>_images/<name>_<W>x<H>_<dpi>dpi.<ext>` (overwrite ഇല്ല),
  ImageMagick → JPEG Q92, PNG alpha, CMYK/Gray, ICC, EXIF അതേപടി; page look അതേപടി (scale/offset); feather/contour ഒപ്പം; ഒറ്റ undo.
- **RS** button → Image Size dialog (file info: px, stored dpi, inch size, effective dpi colour, MB/format/mode/alpha/ICC; Fit to frame @ dpi; JPEG quality; import offer).
- **Extras → Optimize Images…**: selected/whole document pictures list (px, dpi colour, MB), target dpi, tick above target, Apply (progress, ഒറ്റ undo), MB before → after.
  Commit `198b415`.

### 70. Cursor — crop/BG/DC-ന് ശേഷം resize/move cursor പോകുന്ന bug
- DC contour mode canvas widget-ൽ തന്നെ cursor set ചെയ്തിരുന്നത് (Arrow) പിന്നീട് എല്ലാ resize/move cursor-ഉം മറയ്ക്കുമായിരുന്നു — ശരിയാക്കി. Override cursor-കൾ RAII guard-ൽ;
  BG/crop/feather/resample/DC കഴിഞ്ഞാൽ cursor ഉടൻ ശരിയാകും; stuck override ഉണ്ടെങ്കിൽ clear. Debug: `SCRIBUS_DEBUG_CURSOR=1 scribus`. Commit `cb2c6e4`.

### 71. Toolbar — control bar / arrange bar / image size bar re-pack, auto-fit, resize
- Control bar 1366 px-ൽ ഒതുങ്ങും (text / editing / image states). Arrange bar (`suneerarrangebar`), frame size bar, image size bar പുതുക്കി; style select,
  Properties XYZ, resize gesture, page item auto-fit മാറ്റങ്ങൾ.
- Image row-ലെ "Edit current selected style" / "Add new line style" ബട്ടണുകൾ നീക്കി (Edit → Styles, Properties palette-ൽ ഉണ്ട്).
- Gap + / − (all sides): ബട്ടൺ size മാറ്റാതെ വലിയ bold glyph (SVG, theme നിറം, disabled മങ്ങിയത്, HiDPI crisp).
- 8 per-side gap ബട്ടണുകൾക്ക് പകരം **Gap sides ▾** popup (`suneergappopup`): frame diagram, ഓരോ side-നും − / value (mm, Enter) / +,
  Link all sides, Step (text 0.5 mm, image 1 mm, ഓർക്കും), Reset; പഴയ slots, ഒരു click / typed value = ഒരു undo. Toolbar row 2: 167 px കുറഞ്ഞു.

### 72. Drag redraw + News Browser status bar
- Drag ചെയ്യുമ്പോൾ text frame layout ഒഴിവാക്കിയിരുന്നത് മാറ്റി (re-entrancy limit മാത്രം); obit flags canvas overlay; News Browser-ൽ status, progress bar,
  Details… dialog. Debug: `SUNEER_DRAG_DEBUG=1`.

### 73. Edit Photo, Photo background colour, photo store
- **Item › Image › Edit Photo…** (`suneerphotoedit`, dialog): ORIGINAL picture-ൽ non-destructive crop / rotate / flip / auto face / background remove
  (crop-region rembg, cache), lasso, smooth brushes, refine edge, Restore/Remove selection, Smart restore, Protect/Exclude mask, view modes (M),
  Background: Auto (AI) / Manual / None, feather 0–20 px. Sidecar `<edited>.json`; reopen → അതേ state.
- **Photo Background Colour…** (BGc): തിരഞ്ഞെടുത്ത cut-out photos-ന് പിന്നിൽ ഒരു നിറം, live preview, screen picker.
- **Resize Image File to Frame Resolution…**
- **Photo store**: `<sla>_photos/original|edited`, relative links, News photos embed ചെയ്യില്ല; Save As folder copy; `_images` migrate;
  open-ൽ missing photos ചോദ്യം; Extras › Re-download Missing Photos / Remove Unused Photos…; Collect for Output folder copy.
- Obituary: background removal policy auto / ask / manual (Settings), "M" badge, "check BG" flag; re-process manual / none തൊടില്ല.

### 74. Layout spacing — ഒരിടത്ത്
- `<prefs>/layout-spacing.json` (`suneerspacing`): module gap V/H, photo–text, wrap pad, caption gap, caption–text, headline–body, inset,
  column gap, separator rule (none / 0.25 / 0.5 pt) + colour; model override (Normal, Obituary 2.3 / 3.0, Auto Layout).
- Preferences → **Faircode Layout: Spacing**, News Browser-ൽ ⇕ ബട്ടൺ: live preview, Apply to this page (ഒറ്റ undo), Cancel restore, Export / Import.
- Normal / Obituary placement, re-pack, gap check (module gap + 1.7 mm) എല്ലാം ഈ values ഉപയോഗിക്കുന്നു.

### 75. "File → New-ൽ page കാണുന്നില്ല" — prefs self-repair
- കാരണം: `prefs172.xml`-ലെ palette layout (`ads_dockstate`)-ൽ document area ഇല്ലാതായത്. Scribus തുറന്നിരിക്കെ logout / shutdown ചെയ്യുമ്പോൾ window
  രണ്ടാമതും close ആയി ഒഴിഞ്ഞ layout save ആയിരുന്നു. ഇനി രണ്ടാമത്തെ close ഒന്നും save ചെയ്യില്ല; document area ഇല്ലാത്ത layout save / restore ചെയ്യില്ല
  (default layout).
- Startup repair (update കഴിഞ്ഞ് ആദ്യ start-ൽ തനിയെ): duplicate PageSets നീക്കും, കേടായ layout ഒഴിവാക്കും, undo history 0 → 100 (user 0 വെച്ചതല്ലെങ്കിൽ),
  invalid page layout → Single Page, grid / scale / pasteboard values. ഉടനെ save ചെയ്യും; `~/.config/scribus/prefs-repair.log`-ൽ ഒരു വരി.
- Preferences → OK ഇനി values മാറ്റില്ല (20 → 20.001, PolygonFactor 0.502, markLength 20.0013 ഇല്ല); PDF JPEG compression ഇനി restart-ൽ Automatic ആകില്ല;
  UseDocBleeds, show-splash ശരിയായി ഓർക്കും.
- **Help → Reset Preferences…**: `~/.config/scribus` → `scribus.bak-<date>`, defaults-ൽ Scribus വീണ്ടും തുടങ്ങും.
- Test: `tests/suneer/prefs_roundtrip.sh <profile> 20` — 20 തവണ load + Preferences OK + save, warm-up-ന് ശേഷം files byte for byte ഒന്ന്.

### 76. Layout Models — Spacing page-ന് പകരം (Phase 1)
- ഓരോ layout model-ഉം ഒരു JSON file: `~/.config/scribus/layout-models/<id>.json` (`suneerlayoutmodel`): name, page types, grid (columns,
  column width, gutter, margins), structure (decks, left / right rail, lead position + width), story width by body length, headline style
  by width, photo width / position / max height, എല്ലാ spacing values-ഉം, Obituary model-ന്റെ photo / face / background / alignment.
- Built-in: **Normal (4+3)**, **Obituary**, **Pradeshikam (learned)** (`autolayout-rules.json`-ലെ അളവുകൾ). പഴയ `layout-spacing.json`, `obituary.json`
  ആദ്യ start-ൽ ഇവയിലേക്ക് മാറും (പഴയവ `.migrated-<date>` / `.pre-models-<date>.bak` ആയി സൂക്ഷിക്കും).
- **Layout → Layout Models…** (News Browser ⇕): New / Duplicate / Rename / Delete / Export / Import / Restore defaults; spacing diagram
  (ഓരോ gap-ഉം dimension line + value, mm, 0.1 step); real text engine-ൽ Malayalam sample page preview; Apply spacing to this page.
- **Layout → Auto Layout…** (News Browser ▦): Layout Gallery — page type filter, Keep (Placed frames / Every frame on the page / Only new stories),
  ഓരോ model-നും current page-ന്റെ thumbnail + "24 of 34 fit · 38 pt free"; background-ൽ render ചെയ്യും. Apply = ആ page type-ന് model ഓർക്കും
  (placed stories re-arrange Phase 2-ൽ).
- Preferences → Faircode Layout: Spacing page നീക്കി.

### 77. Layout Models — എല്ലായിടത്തും (Phase 2)
- News Browser dropdown-ൽ page type-ന് ചേരുന്ന എല്ലാ layout models-ഉം (ഓർക്കുന്നത് page type അനുസരിച്ച്). Place / Place Selected ആ model ഉപയോഗിക്കും:
  Normal = പഴയ പോലെ (story spike / page stack), Obituary engine models = obit placement, മറ്റ് news models = page-ൽ നേരിട്ട് arrange.
- **Re-layout page with…** (News Browser ബട്ടൺ, page / story right-click): page-ലെ stories മറ്റൊരു model-ൽ — text, photos, captions
  അതേപടി; കൈകൊണ്ട് വെച്ചവയും Pin ചെയ്തവയും നിൽക്കും; fit ആകാത്തവ pasteboard-ൽ, list ചെയ്യും; ഒറ്റ undo. Gallery-യിലെ Apply ഇതുതന്നെ ചെയ്യും.
- Headline: News Browser / model style ആണെങ്കിൽ മാത്രം model-ന്റെ width style (undo ചെയ്യാം); user മാറ്റിയ style തൊടില്ല.
- Grid, story / photo width, photo max height, spacing, pasteboard gaps, obit values: active model-ൽ നിന്ന് മാത്രം.

### 78. Archive variants — നമ്മുടെ പഴയ pages-ൽ നിന്ന് models (Phase 3)
- Layout Gallery → **Generate from archive…**: ഒരു folder-ലെ .sla pages (file name filter optional) വായിച്ച്
  (`tools/autolayout/learn_models.py`, analyze.py-യുടെ അളവുകൾ; network / AI ഇല്ല) grid കണ്ടെത്തും, pages-നെ structure അനുസരിച്ച്
  (lead position, rails, decks) groups ആക്കും, ഓരോ group-ഉം ഒരു model (`archive-<type>-a/b/c.json`, origin "archive") — Gallery-യിൽ extra cards.
- Width by length, headline style by width, lead headline, photo width / position / max height, vertical gap, column gap: ആ pages-ൽ നിന്ന് അളന്നത്.
- Obituary pages-ഉം ഇനി പഠിക്കും (Phase 3+): `analyze.py` obit modules (photo + പേര് / സ്ഥലം / വിവരം) തിരിച്ചറിയും — styles വഴി,
  പഴയ pages-ൽ geometry വഴി. Obit pages-ൽ നിന്ന് Obituary engine model (columns, obit / news columns, photo size / shape, gaps, styles,
  alignment); photo processing settings ഇപ്പോഴത്തെ Obituary model-ൽ നിന്ന്. 1.7 SLA-യിലെ text ഇപ്പോൾ ശരിയായി വായിക്കും.

### 79. Layout Model ഏത് page-നും, place ചെയ്ത ശേഷവും
- **Layout → Apply Layout Model…** (News Browser / login / network വേണ്ട), page-ൽ right-click → Apply Layout Model… / Page Modules…,
  News Browser **Re-layout…** (Layout Gallery, Page Modules, models). Gallery-യിൽ **Apply to selection only** (തിരഞ്ഞെടുത്ത stories, അവയുടെ
  ഇപ്പോഴത്തെ area-ക്കുള്ളിൽ), **Compare** (ഇപ്പോഴത്തെ page / പുതിയത്), **Check modules…**.
- കൈകൊണ്ട് ഉണ്ടാക്കിയ / പഴയ SLA pages-ൽ modules തനിയെ കണ്ടെത്തും: body frame + headline frames + photos + captions; groups, obits;
  locked, ads, masthead / folio, rules, master items, chains ഒരിക്കലും നീക്കില്ല. **Layout → Page Modules…**: നിറമുള്ള outlines + എണ്ണം,
  Merge / Split / Keep fixed; frames-ൽ `layout.module` attributes ആയി ഓർക്കും.
- Re-layout: text / photo edits മാറില്ല; frames, columns, photo, caption, spacing model അനുസരിച്ച്; fit ആകാത്തവ pasteboard-ൽ (list); ഒറ്റ undo.

### 80. Layout Gallery — ഓരോ card-ഉം page-ന്റെ ഒരു copy-യിൽ ശരിക്കും arrange ചെയ്തത്
- ഓരോ card: page-ന്റെ ഒരു മറച്ച copy (Scribus copy / paste, layer തിരിച്ച്; styles, colours, guides, text settings) — Apply ഓടിക്കുന്ന
  അതേ code (`arrangePage`) ആ copy-യിൽ ഓടിക്കും, ആ copy വരച്ചതാണ് thumbnail. Fit line ("6 of 9 fit · 339 pt free · N overflow") ആ copy-യിലെ
  ശരിയായ frames-ൽ നിന്ന്. Undo ഇല്ല, network ഇല്ല (പുതിയ stories-ന്റെ pictures copy ഉണ്ടാക്കും മുമ്പ് എടുക്കും).
- Apply കഴിഞ്ഞാൽ report: "✔ The page now matches the card exactly" — അല്ലെങ്കിൽ എത്ര frames വ്യത്യാസം.
- Cards ഒന്നൊന്നായി (spinner: arranging… / waiting…); cache (page state + model + Keep) — വീണ്ടും തുറക്കുമ്പോൾ ഉടൻ.
- രണ്ട് models ഒരേ ഫലം തന്നാൽ "Same result as …"; page മാറുന്നില്ലെങ്കിൽ "No change to the page".
- Keep = "Every frame on the page", പുതിയ stories ഇല്ല: എല്ലാ cards-ഉം ഒരുപോലെ ആകും — ഇപ്പോൾ ചുവപ്പിൽ കാരണം പറയും.
- Copy / paste ചില texts കൃത്യമായി കൊണ്ടുപോകില്ലായിരുന്നു (അവസാന paragraph / default style) — copy-യിലെ text page-ൽ നിന്ന് നേരിട്ട് എടുക്കും.
  പഴയ Scribus line breaking (`SuneerClusterLevel=chars`) copy-ക്കും അളക്കലിനും.

### 81. Layout Gallery Apply crash — ശരിയാക്കി
- Gallery card-നുള്ള page copy ഉണ്ടാക്കുമ്പോൾ Guide Manager palette-ന് ആ copy കൊടുത്തിരുന്നു; copy delete ചെയ്ത ശേഷവും palette അത്
  ഓർത്തു, പിന്നെ delete ആയ memory-യിൽ എഴുതി → കുറച്ച് കഴിഞ്ഞ് മറ്റെവിടെയെങ്കിലും crash (Apply കഴിഞ്ഞ്). ഇനി copy palettes-ലേക്ക് പോകില്ല.
- News Browser-ൽ പുതിയ stories ഉള്ളപ്പോൾ Gallery cards ഉണ്ടാക്കുമ്പോൾ crash (copy-ക്ക് hyphenator ഇല്ല) — ശരിയാക്കി.
- Apply-ക്ക് മുമ്പ് model-ഉം modules-ഉം പരിശോധിക്കും (grid, margins, gaps, frames ഉണ്ടോ); പ്രശ്നമുണ്ടെങ്കിൽ ഒന്നും മാറ്റാതെ വ്യക്തമായ message.
- Card-ഉം Apply ഫലവും ഇപ്പോൾ എല്ലാ models-ലും കൃത്യമായി ഒന്ന് (captions, fonts, obit blocks, settle order).
- Undo ഇപ്പോൾ caption-ന്റെ bottom text distance-ഉം തിരികെ കൊണ്ടുവരും. Text distance box (മറച്ചത്) selection-ൽ frame-ലേക്ക് എഴുതില്ല.
- Test: `tests/suneer/layout_models_apply.sh` — ഓരോ model-ഉം ഓരോ page-ൽ: card → Apply → card-ഉമായി താരതമ്യം → Undo → page പഴയപടി.

### 82. Embed All — document-ലെ എല്ലാ images-ഉം .sla-യുടെ ഉള്ളിലേക്ക്
- Image toolbar 📦 → **Embed in SLA / Embed All**, Extras → Embed All Images: ഇപ്പോൾ എല്ലാ images-ഉം embed ചെയ്യും — News Browser photos
  (`<sla name>_photos/original`, `edited`), Edit photo ഫലം, BG remove ചെയ്ത PNG (transparency അതുപോലെ), obituary photos, groups / modules-ന്
  ഉള്ളിലുള്ളവ, master pages, pasteboard, text-ലെ inline frames, patterns. മുമ്പ് `_photos` folder-ലെ files ഒന്നും പറയാതെ ഒഴിവാക്കിയിരുന്നു.
  (Place ചെയ്യുമ്പോഴുള്ള automatic embed News Browser photos-നെ ഇപ്പോഴും link ആയി തന്നെ വിടും.)
- Image frames select ചെയ്തിട്ടുണ്ടെങ്കിൽ ചോദിക്കും: "Selected only" / "All N images in document". Progress bar; ഒറ്റ undo.
- Message: "Embedded 34 image(s) (+12.4 MB). 2 skipped:" — missing files frame name + page സഹിതം.
- Frame-ന്റെ attributes (original, edited, settings) മാറില്ല: embed ചെയ്ത ശേഷവും Edit photo original തുറക്കും. പുതിയ attribute
  `suneer.embeddedFrom` = embed ചെയ്ത file.
- പുതിയത്: 📦 → **Unembed / Extract to _photos/edited** — embedded pictures `<sla name>_photos/edited/`-ലേക്ക് എഴുതി frame link ചെയ്യും
  (ഒരേ file ഉണ്ടെങ്കിൽ അത് തന്നെ ഉപയോഗിക്കും). Selected only / All; ഒറ്റ undo.
- Test: `tests/suneer/embed_all_images.sh page.sla` — embed → save → .sla മാത്രം മറ്റൊരു folder-ലേക്ക് → തുറന്ന് render (pixel താരതമ്യം) → extract.

### 83. Gaps — ഓരോ Layout Model-നും, place ചെയ്ത ശേഷവും
- Layout Models editor-ൽ പുതിയ **Gaps** tab: Headline → body text, Subheading / kicker → headline (പുതിയത്), Body column gutter,
  Photo → text (beside / above-below), Photo → caption, Caption → text, Story to story (horizontal / vertical). ഓരോന്നും 0.1 mm
  step, ഓരോന്നിനും ↺ "Reset to house default" (`autolayout-rules.json`-ൽ നിന്ന്; അതിൽ ഇല്ലാത്തവ built-in default). Model JSON-ൽ save.
- Gallery-യിൽ നിന്ന് Layout Models… തുറന്ന് gap മാറ്റുമ്പോൾ ആ model-ന്റെ card ഉടൻ വീണ്ടും വരയ്ക്കും (save ചെയ്യാതെയും).
- Place ചെയ്ത story-യിൽ right-click → **Adjust gaps…**: മാറ്റം page-ൽ ഉടൻ കാണാം, OK = ഒറ്റ undo step, Cancel = page പഴയപടി.
  "Apply to all stories on this page" (story-to-story gaps ഉൾപ്പെടെ), "Save these gaps to the model".
- Story-കൾ അവയുടെ model-ഉം gaps-ഉം ഓർക്കും (`layout.model`, `layout.gaps`, `layout.gapsUser`); Re-layout user-ന്റെ gaps നിലനിർത്തും.
- Gap കാരണം ഒരിക്കലും overlap ഇല്ല: story അടുത്ത frame-ന് മുമ്പ് നിൽക്കും, text കയറാതെ വന്നാൽ overflow marker; page-ൽ സ്ഥലമില്ലെങ്കിൽ pasteboard-ലേക്ക് (list).
- Test: `tests/suneer/layout_gaps.sh page.sla`.

### 84. Empty lines at the end of news text — cleaning on import, frames fitted to the last line

- API text-ന്റെ അവസാനത്തെ Enter / ഒഴിഞ്ഞ paragraphs / zero-width characters (`\n​`), spaces, `<br>`, `<p>&nbsp;</p>` എല്ലാം
  place ചെയ്യുമ്പോൾ തന്നെ മാറ്റും — News Browser placement, Obituary model, Layout Gallery / Apply Layout Model, Re-layout —
  headline, kicker, byline, place, caption, body എല്ലാ fields-ഉം. ഉള്ളിലെ 2+ ഒഴിഞ്ഞ paragraphs → ഒരു paragraph break.
  Soft hyphens-ഉം അക്ഷരത്തിനുള്ളിലെ ZWJ/ZWNJ-ഉം തൊടില്ല.
- Story frame-ന്റെ അവസാനത്തെ paragraph mark-ഉം (ഒരു ഒഴിഞ്ഞ വരി) ഇനി ഇല്ല; frame അവസാന വരിയിൽ fit ആകും
  (bottom text distance ഉൾപ്പെടെ; പഴയ Fit Frame Height അത് രണ്ടാമതും + 2 pt കൂട്ടിയിരുന്നു).
- **Extras → Clean Empty Lines & Fit Frames** (page-ലെ right-click-ലും, text frame-ന്റെ right-click-ലും): select ചെയ്ത text frames,
  അല്ലെങ്കിൽ ഒന്നും select ചെയ്തില്ലെങ്കിൽ ഈ page-ലെ News Browser / Layout Model frames. ഒഴിഞ്ഞ വരികൾ മാറ്റി, frame-ന്റെ
  ഉയരം താഴത്തെ വരിയിലേക്ക് ചുരുക്കും — ഓരോ വരിയും ഉള്ളിടത്ത് തന്നെ (columns വീണ്ടും balance ചെയ്യില്ല, text വെട്ടില്ല, overflow
  ഉള്ള frame വലുതാക്കില്ല). ഒറ്റ undo. Message "Cleaned 23 frames …".
- api.log-ൽ "text clean ..id: content (end: LF U+200B) | title (end: SP)" — ഏത് API field-ൽ എന്ത് ഉണ്ടായിരുന്നു.
- Test: `tests/suneer/clean_empty_lines.sh page.sla`.

### 85. Page Models — പഴയ page-കളുടെ layout മുഴുവനായി model ആക്കി, page group-കളിൽ; പുതിയ page-ൽ skeleton, slot-ൽ story

- **Layout → Page Models…** (Layout Gallery-യിലും "Page Models…" / "Create Page Model…" buttons). ഇടത്ത് page group list
  (Page-1 … Page-24, Obituary, Editorial, Sports, Business, Supplement — "Page-N count…" കൊണ്ട് എണ്ണം മാറ്റാം; New / Rename /
  Note / grid / Delete / ▲▼), വലത്ത് ആ group-ലെ page model-കൾ വലിയ card-കളായി (name, stories · photos · ad size,
  "fits current ads" filter). Card-ൽ right-click: Rename, Move to group, Duplicate, Delete (confirm), Export this model.
  Export… / Import…: ഒരു group അല്ലെങ്കിൽ എല്ലാ group-കളും ഒറ്റ JSON file (മറ്റ് office PC-കളിലേക്ക്). Group-ന് note-ഉം grid type-ഉം:
  normal = 8 column (34.25 mm, gutter 8 mm); Obituary = 10 × 30.3 mm obit grid.
- **Create Page Model…**: പഴയ .sla file-കൾ (ഒന്നോ പലതോ, അല്ലെങ്കിൽ folder മുഴുവൻ; 1.4 / 1.5 / 1.7) → "Read pages" → review list:
  thumbnail | file / page (multi-page file-ൽ ഓരോ page-ഉം ഒരു വരി, വേണ്ടാത്തത് untick) | group (dropdown) | model name | content →
  "Import ticked pages" (progress bar). Group auto-suggest: page-ലെ obituary module-കൾ (≥ 6 → Obituary), file name ("21page1" →
  Page-1, "…Obit…" → Obituary, "SPO" → Sports, "edit" → Editorial …), multi-page file-ലെ page number. ഒരേ group-ൽ ഏതാണ്ട് ഒരേ model
  (≥ 90 %) ഉണ്ടെങ്കിൽ ചോദിക്കും: Skip / Keep both / Skip all / Keep all.
- Model-ൽ ഉള്ളത്: ഓരോ story-യും grid-ലെ ഒരു **slot** (columns, top / height mm, headline style · size · lines, body columns, photo
  slot size · position), importance order (slot 1 = lead), obituary modules, ad areas ("ad space" മാത്രം), fixed items (masthead /
  folio), rules, boxes. **Slot-കൾ ഒരിക്കലും overlap ആകില്ല** (ഒരു photo ഒരു story-ക്ക് മാത്രം; overlap വന്നാൽ താഴ്ന്ന slot വെട്ടും).
  **പഴയ text-ഉം photo-ഉം ഒരിക്കലും save ചെയ്യില്ല**; thumbnail grey box-കൾ മാത്രം. Reader: `tools/autolayout/page_models.py` (വേറെ process).
- **Skeleton on this page** (card button): page-ൽ ഓരോ slot-നും ഒരു placeholder frame (light grey, dashed border, ഉള്ളിൽ
  "Slot 3 – 4 col + photo 3 col"), model-ന്റെ ad space outline മാത്രം — എല്ലാം non-printing layer **"Page model"**-ൽ (print / PDF-ൽ
  വരില്ല). Page-ൽ ഉള്ള frames / ads ഒന്നും നീക്കുകയോ delete ചെയ്യുകയോ ഇല്ല. Skeleton ഉണ്ടെങ്കിൽ "Replace skeleton?" ചോദിക്കും. ഒറ്റ undo.
  Group name page-ൽ save (`SuneerGroup`) — അടുത്ത തവണ gallery ആ group-ൽ തുറക്കും.
- **Slot നിറയ്ക്കൽ**: News Browser list-ൽ നിന്ന് story placeholder-ലേക്ക് **drag** ചെയ്യുക, അല്ലെങ്കിൽ placeholder select ചെയ്ത് ഒരു story
  tick ചെയ്ത് **Place Selected** (placeholder-ൽ right-click → "Place selected story here"-ഉം). Headline / body / photo / caption
  ആ slot rectangle-ൽ തന്നെ: headline style slot-ന്റെ വീതി അനുസരിച്ച് house rule (Normal / Obituary layout model), photo columns-ഉം
  position-ഉം ഉയരവും slot-ൽ നിന്ന്, text fit (അവസാനത്തെ ഒഴിഞ്ഞ വരികൾ നീക്കി). നീളം കൂടിയാൽ frame slot-ൽ വെട്ടി **ചുവപ്പ്** outline
  (text കളയില്ല, overflow ആയി നിൽക്കും); 8 mm-ൽ കൂടുതൽ കുറവ് → **മഞ്ഞ** outline. Slot-ന് പുറത്തേക്ക് ഒരിക്കലും പോകില്ല. Placeholder
  അപ്പോൾ മാറും. ഓരോ slot-ഉം ഒറ്റ undo.
- **Layout → Remove Empty Slots** (gallery-യിലും, placeholder right-click-ലും): ബാക്കി placeholder-കളും ad outline-കളും delete.
- Storage: `~/.config/scribus/layout-models/pages/<group>/<model>.json` + `<model>.png`; `groups.json`.
- Tests: `tests/suneer/page_models_import.sh <folder>` (overlap check ഉൾപ്പെടെ); `tests/suneer/page_models_apply.sh --models <pages dir>
  --model <group>/<id> --tick N page.sla fixture.json` (skeleton → N slots fill → remove → undo).

### 86. Obituary page — വലതുവശം (columns 9–10) 2-column news-ന് മാത്രം
- Workflow-ൽ column count **2** ഉള്ള stories (News.noOfColumns = 2, അല്ലെങ്കിൽ News Browser list-ലെ "Place as ▸ 2 columns") മാത്രം
  columns 9–10-ൽ ("Obituary – news left" model-ൽ columns 1–2). അങ്ങനെ ഒരു story-യും ഇല്ലെങ്കിൽ ആ columns **blank** — obit modules
  അവിടെ വരില്ല. Message: "Columns 9-10: no 2-column news (workflow count 2) … left blank".
- Obituary modules-ഉം മറ്റ് news-ഉം (count ഇല്ലാത്തത് = 1 column; 3+ = ആ count) columns 1–8-ൽ മാത്രം. 1–8 നിറഞ്ഞാൽ 9–10-ലേക്ക്
  കയറില്ല — pasteboard-ൽ park ചെയ്ത് message-ൽ list: "No room in columns 1-8 … (columns 9-10 are only for 2-column news)".
- 9–10-ൽ കൊള്ളാത്ത 2-column stories 1–8-ലേക്ക് മാറ്റില്ല; message-ൽ വേറെ list: "2-column news with no room in columns 9-10 …
  not moved into columns 1-8".
- എല്ലാ വഴികളിലും ഒരേ rule: Place Selected, Layout Gallery-യിലെ Obituary cards (Apply), Re-layout (news right / news left),
  Re-pack (blank band empty area ആയി കണക്കാക്കില്ല; "left blank" line).
- പഴയ pages (count save ചെയ്യാത്തവ): 2 column വീതിയുള്ള news module band story ആയി കണക്കാക്കും — Re-layout-ൽ അവ 9–10-ൽ തന്നെ.
- Place ചെയ്ത news frame-ൽ `news.columns` attribute (workflow count) save ചെയ്യും; Re-layout / Apply-ൽ rule നിലനിൽക്കും.
- ശ്രദ്ധിക്കുക: `/external/edition-pages/:id/news` API reply-യിൽ noOfColumns ഇല്ല (backend-ൽ ഒരു വരി fix വേണം); login വഴി
  (`/news/layout`) ഉണ്ട്. അതില്ലാത്തപ്പോൾ "Place as ▸ 2 columns" ഉപയോഗിക്കുക.

### 87. Attached caption — Undo / Redo-ലും caption photo-യുടെ താഴെ തന്നെ
- Photo-യുടെ caption (News Browser place ചെയ്തത്) photo-യുടെ താഴെ, അതേ വീതിയിൽ, caption gap വിട്ട് — photo resize, Undo, Redo
  എല്ലാത്തിലും.
- Layout Gallery Apply / Re-layout (Normal, news left) → Edit > Undo: captions ഇപ്പോൾ Apply-ക്ക് മുമ്പുള്ള സ്ഥലത്ത് കൃത്യമായി
  (മുമ്പ് ചില captions photo-യിൽ നിന്ന് മാറി താഴെ നിന്നിരുന്നു).
- Action History-യിൽ പല steps ഒന്നിച്ച് undo / redo ചെയ്താലും ഫലം ഓരോ step-ഉം ഒറ്റയ്ക്ക് Undo അമർത്തിയതുപോലെ തന്നെ.

### 88. Attached caption — save ചെയ്ത് വീണ്ടും തുറന്നാലും caption അതിന്റെ photo-യെ വിടില്ല
- ഓരോ photo-യ്ക്കും സ്ഥിരമായ ഒരു id (`news.photoUid`) — place ചെയ്യുമ്പോൾ തന്നെ. Caption-ൽ അതേ id (`news.captionOf`). Photo-യുടെ
  പേര് (Image130 പോലുള്ളവ) ഇനി link അല്ല — file തുറക്കുമ്പോൾ Scribus പേരുകൾ വീണ്ടും ഉണ്ടാക്കും, മുകളിലെ frames delete ചെയ്താൽ
  മറ്റൊരു photo-യ്ക്ക് ആ പേര് കിട്ടും (അപ്പോൾ resize ചെയ്ത photo-യ്ക്ക് പകരം വേറെ story-യുടെ caption നീങ്ങിയിരുന്നു).
- പഴയ files: തുറക്കുമ്പോൾ story + photo index നോക്കി ശരിയായ caption കണ്ടെത്തി id കൊടുക്കും; അടുത്ത save-ൽ file-ൽ എഴുതും.
- Copy/Paste, Duplicate, Page copy, Import Page: copy ചെയ്ത photo-യ്ക്ക് പുതിയ id, അതിന്റെ caption-ഉം copy-യുടെ കൂടെ. Caption മാത്രം
  copy ചെയ്താൽ അത് photo-യിൽ attach ആകില്ല. Cut + Paste-ൽ link അതേപടി.
- പഴയ version-ൽ copy ചെയ്ത് ഒരേ id വന്ന photos: തുറക്കുമ്പോൾ weld / സ്ഥാനം നോക്കി വേർതിരിക്കും.
- "Keep caption attached" off / on, welds — save + reopen കഴിഞ്ഞും അതേപടി.

### 89. Attached caption — Undo / Redo-നു ശേഷം caption-ന്റെ ഉയരവും ശരി
- Photo resize ചെയ്ത് Undo ചെയ്താൽ caption അതിന്റെ സ്ഥാനത്ത്, ശരിയായ ഉയരത്തിൽ (മുമ്പ് ചിലപ്പോൾ resize-ന്റെ സമയത്തെ ഉയരം തന്നെ
  നിന്നിരുന്നു — ഉദാ: രണ്ട് steps undo ചെയ്തപ്പോൾ 3 mm കൂടുതൽ).
- Photo വളരെ ചെറുതാക്കി (caption-ന് കൂടുതൽ വരികൾ വേണ്ടപ്പോൾ) caption ഇനി മുഴുവൻ text കാണിക്കുന്ന ഉയരത്തിലേക്ക് വളരും; മറ്റൊരു
  story-യുടെ frame-ന്റെ wrap caption-ന്റെ വരികൾ ദൂരേക്ക് തള്ളുമ്പോൾ മാത്രം (runaway) സ്വന്തം text-ന്റെ ഉയരം + overflow mark.
- Story മുഴുവൻ നീക്കുമ്പോൾ caption fit ചെയ്യില്ല (മുമ്പത്തെ പോലെ); അത് മറ്റൊരു frame-ന്റെ മുകളിൽ വന്നാൽ അടുത്ത resize-ൽ fit ആകും.

### 90. Manual photo + caption (Insert Image, embed) — reopen-നു ശേഷവും caption കൂടെ; weld point ശരിയാക്കൽ; photo delete-ൽ ചോദ്യം
- News Browser വഴി അല്ലാതെ വെച്ച photo-കളും (Insert Image, embed ചെയ്തവ) അവയുടെ `caption_<photo>` frame-ഉം uid വഴി link ആകും. പഴയ files
  തുറക്കുമ്പോൾ: പേര്, അല്ലെങ്കിൽ photo-യുമായുള്ള weld, അല്ലെങ്കിൽ caption photo-യുടെ തൊട്ടുതാഴെ (അതേ x, അതേ വീതി ±1.5 mm) — ഇവയിൽ ഒന്ന്.
- Weld point-കൾ: photo-യും caption-ഉം ചേരുന്ന അരികിൽ (photo-യുടെ താഴെ ഇടത്, caption-ന്റെ മുകളിൽ ഇടത്). Frame-ന് പുറത്തുള്ള weld points
  file തുറക്കുമ്പോൾ ശരിയാക്കും.
- Photo delete ചെയ്യുമ്പോൾ (Edit > Delete, Del key) അതിന്റെ caption select ചെയ്തിട്ടില്ലെങ്കിൽ ചോദിക്കും: "Delete Caption Too" / "Keep
  Caption" / Cancel. Keep: caption അവിടെ തന്നെ; Undo ചെയ്താൽ photo-യും link-ഉം തിരികെ.
- **Extras > Clean Orphan Captions…**: photo ഇല്ലാത്ത caption frames കാണിച്ച് delete ചെയ്യും (ഒറ്റ Undo-ൽ തിരികെ). File തുറക്കുമ്പോൾ
  അങ്ങനെയുള്ളവ ഉണ്ടെങ്കിൽ status bar-ൽ പറയും.
- പഴയ file-ലെ caption photo-യിൽ നിന്ന് എത്ര അകലത്തിലാണോ (ഉദാ: 1 mm, 0.2 mm) ആ gap തന്നെ ഇനി അതിന്റെ caption gap — ആദ്യ resize-ൽ
  caption ചാടില്ല. Undo-നു ശേഷം caption ഉയരം വീണ്ടും fit ചെയ്യുന്നത് caption നീങ്ങുമ്പോൾ / വീതി മാറുമ്പോൾ മാത്രം.

### 91. Design Style shortcut — save ചെയ്ത key പ്രവർത്തിക്കും, dialog വീണ്ടും തുറന്നാലും കാണും
- Style Settings > Design Style-ൽ കൊടുക്കുന്ന Shortcut ഇനി ശരിക്കും പ്രവർത്തിക്കും: frame select ചെയ്ത് key അമർത്തിയാൽ ആ Design
  Style icon click ചെയ്തതുപോലെ തന്നെ (text edit mode-ൽ select ചെയ്ത text-ന് മാത്രം), ഒറ്റ Undo. Save ചെയ്ത ഉടൻ — restart വേണ്ട.
- Dialog വീണ്ടും തുറക്കുമ്പോൾ save ചെയ്ത key field-ൽ കാണും (മുമ്പ് field കാലിയായിരുന്നു, അടുത്ത Save key മായ്ച്ചുകളഞ്ഞിരുന്നു). "Clear" button.
- Key വേറെ എവിടെയെങ്കിലും ഉപയോഗത്തിലുണ്ടെങ്കിൽ (paragraph style, Column config, menu, മറ്റൊരു Design Style) field-നു താഴെ ചുവപ്പിൽ
  പറയും; Save-ൽ ചോദിക്കും: Replace / Choose another key / Cancel. Replace പഴയ സ്ഥലത്തുനിന്ന് key മാറ്റും.
- Design Style tab-ൽ ഓരോ icon-നു താഴെ പേരും shortcut-ഉം (`[Ctrl+Alt+3]`).

### 92. Obituary photo — ശരിയായ മുഖം, മുടി മുറിയില്ല, എല്ലാ photo-യിലും ഒരേ മുഖ വലുപ്പം
- ഷർട്ടിനെയോ കൈയെയോ മുഖമായി എടുത്ത് താടി/ഷർട്ട് മാത്രം കാണിച്ചിരുന്നത് ശരിയാക്കി: ഏറ്റവും വലിയ box അല്ല, കണ്ണ്, skin,
  രണ്ട് detector-ഉം സമ്മതിക്കുന്നത് നോക്കി മുഖം തിരഞ്ഞെടുക്കും. Edit photo-യുടെ face guide-ഉം ഇതേ മുഖം.
- മുടിയുടെ മുകളിൽ ചെറിയ ഇടം; തലയുടെ മുകൾ ഇനി മുറിയില്ല (ആവശ്യമെങ്കിൽ crop മുകളിലേക്ക് / അല്പം ചെറുതായി).
- Low res photo-കളിലും മറ്റുള്ളവയുടെ അതേ മുഖ വലുപ്പം (orange "low res" flag മാത്രം).
- മുഖം കിട്ടാത്ത photo: ചിത്രത്തിന്റെ മുകൾ ഭാഗം, flag "no face – check" (screen-ൽ മാത്രം).

### 93. Re-pack Page — ഒറ്റ click-ൽ gap അടയ്ക്കൽ (Layout menu, Ctrl+Alt+R, Edit toolbar, page right-click, News Browser-ലെ Re-pack)
- ആദ്യം ഓരോ module-ന്റെയും താഴത്തെ text frame അവസാന വരിയിലേക്ക് fit (text കൂടിയാൽ നീളും, കുറഞ്ഞാൽ ചുരുങ്ങും); photo, caption മാറില്ല.
- Obituary page: obit-കൾ സ്വന്തം column-ൽ മാത്രം മുകളിലേക്ക് (ക്രമം മാറില്ല, column മാറില്ല). 2-column news column 9–10-ൽ
  (left model: 1–2) മാത്രം; ഇല്ലെങ്കിൽ ആ ഭാഗം ഒഴിഞ്ഞു കിടക്കും.
- Normal page: News Browser വെച്ച story-കൾ മാത്രം നീങ്ങും, സ്വന്തം column-ൽ നേരെ മുകളിലേക്ക് (gap 4.8 mm); width, column മാറില്ല.
  Ad, കൈകൊണ്ട് ഉണ്ടാക്കിയ frame, lock / pin ചെയ്തത് നീങ്ങില്ല.
- ഇടം ഇല്ലാതായ module pasteboard-ലേക്ക് (പേരുകൾ message-ൽ); ഒന്നും overlap ആകില്ല. ഒരു Undo step.
- Layout > Re-pack Automatically (default OFF): obituary page-ൽ module delete ചെയ്താലോ text edit കഴിഞ്ഞാലോ ആ column ~1 s കഴിഞ്ഞ്
  re-pack; സ്വന്തം undo step ("Auto re-pack") — ആദ്യ Ctrl+Z re-pack മാത്രം മാറ്റും.
- പുതിയ actions-ന്റെ key: സ്വന്തം shortcut set (dbi) ഉപയോഗിക്കുമ്പോഴും set-ൽ ഇല്ലാത്ത പുതിയ action-ന് default key കിട്ടും
  (ആ key set-ൽ മറ്റൊന്നിന് ഇല്ലെങ്കിൽ).

### 94. Photo badges zoom-നൊപ്പം ചെറുതാകും (LINK, low res, check BG, no face, M)
- Badge-ന്റെ വലുപ്പം photo-യുടെ screen വലുപ്പത്തിനൊപ്പം: Preferences-ലെ size-ഉം photo-യുടെ ഏകദേശം 1/5-ഉം — ഇതിൽ ചെറുത്.
  ഇടം കുറയുമ്പോൾ ചെറിയ label: "low res: 187 ppi (548×548 px)" → "187 ppi" → "!".
- Photo screen-ൽ ~40 px-ൽ താഴെയോ zoom 35 %-ൽ താഴെയോ ആണെങ്കിൽ മൂലയിൽ ഒരു ചെറിയ dot മാത്രം (orange = linked,
  red = missing / check), അല്ലെങ്കിൽ ഒന്നുമില്ല — Preferences > Item Tools > "Small photos".
- Photo select ചെയ്താൽ full label photo-യുടെ മുകളിൽ (മുഖം മറയില്ല); mouse വെച്ചാൽ tooltip-ൽ മുഴുവൻ വിവരം.
- View > Image Frames > Show Photo Badges (Ctrl+Alt+B): എല്ലാ badge-ഉം ഒറ്റയടിക്ക് മറയ്ക്കാം / കാണിക്കാം; setting ഓർക്കും.
- LINK badge ചെറുതും പാതി സുതാര്യവും. Obituary check flag ഇനി ചുവപ്പ് (LINK-ന്റെ orange-ൽ നിന്ന് വേർതിരിക്കാൻ).
- Badge-കൾ screen-ൽ മാത്രം: print, PDF, export, thumbnail-ൽ ഒരിക്കലും ഇല്ല.

### 95. Text-ന് gradient fill — text ആയിത്തന്നെ (edit ചെയ്യാം, PDF-ൽ തിരയാം)
- Properties > Text > "Text Gradient & Shadow", മുകളിലെ toolbar-ലെ "Grad" button, Style Manager (character style-ഉം paragraph
  style-ന്റെ character ഭാഗവും): Text fill = Solid / Gradient (style-ൽ Inherited-ഉം).
- Gradient = document gradient (Edit > Colours and Fills > Gradients). Presets: "Red headline" (കടും ചുവപ്പ് → ചുവപ്പ്),
  "Grey headline" (കടും ചാര → ചാര); തിരഞ്ഞെടുക്കുമ്പോൾ CMYK colours-ഓടെ document-ൽ ചേരും. "Save as preset" — എല്ലാ document-നും.
- Linear (angle, default 90° = മുകളിൽ നിന്ന് താഴേക്ക്) / Radial. Per line (ഓരോ വരിക്കും മുഴുവൻ gradient, അക്ഷരങ്ങളുടെ
  മുകൾ മുതൽ താഴെ വരെ) / Per frame (frame-ലെ മുഴുവൻ text-നും ഒരൊറ്റ gradient).
- Select ചെയ്ത അക്ഷരങ്ങൾക്ക് / മുഴുവൻ frame-ന് / table-ലെ select ചെയ്ത cell-കൾക്ക്; ഒരു Undo step. SLA-ൽ save ആകും.
- PDF: അക്ഷരങ്ങൾ text തന്നെ (font embed, select / search ചെയ്യാം), gradient vector shading, CMYK അതേപടി.
  PostScript / print-ലും gradient.
- അതേ panel-ൽ frame-ന്റെ soft shadow (X, Y, blur, colour, opacity). Blur ഉള്ളതുകൊണ്ട് PDF-ൽ image; PostScript print-ൽ ഇല്ല.

### 96. Obituary photo — OpenCV face data ഇല്ലാത്ത PC-യിലും photo വെക്കും
- Debian-ന്റെ `python3-opencv`-ൽ `cv2.data` ഇല്ല. മുമ്പ് ഓരോ photo-യും `AttributeError: module 'cv2' has no attribute 'data'`
  ആയി fail ചെയ്ത് page-ൽ വന്നില്ല (office PC chn-dtp30, 28-ൽ 28).
- ഇപ്പോൾ face data (haarcascades) `cv2.data`-ൽ ഇല്ലെങ്കിൽ `/usr/share/opencv4/haarcascades` (`opencv-data` package),
  `/usr/share/opencv/haarcascades`, `/usr/local/share/opencv4/haarcascades`, `$SUNEER_HAARCASCADES` എന്നിവ നോക്കും.
- എവിടെയും ഇല്ലെങ്കിൽ: photo BG remove ചെയ്ത്, മുഖമില്ലാത്ത photo പോലെ മുകൾ ഭാഗം crop ചെയ്ത് വെക്കും, ചുവന്ന "no face – check" flag.
  കാരണം message-ൽ: "OpenCV face data (haarcascades) not found …". ഈ ഫലം cache ചെയ്യില്ല — face data install ചെയ്ത ശേഷം
  അടുത്ത placement-ൽ ശരിയായ face crop വരും.
- Edit photo-യിലെ face guide: അതേ വ്യക്തമായ message.

### 97. News Browser login ഒരിക്കലും കുടുങ്ങില്ല — keyring ഇല്ലെങ്കിലും
- ചില PC-കളിൽ (chn-dtp26) login keyring ഇല്ല. Login ശരിയായാലും token keyring-ൽ വെക്കുമ്പോൾ മറഞ്ഞ "new keyring"
  prompt വന്ന് Login dialog എന്നെന്നേക്കുമായി കാത്തിരുന്നു (Cancel-ഉം Login-ഉം ചാരനിറം).
- ഇപ്പോൾ Scribus keyring ഉപയോഗിക്കുന്നത് default keyring ഉണ്ടെങ്കിലും unlock ആണെങ്കിലും മാത്രം (3 സെക്കൻഡിനുള്ളിൽ
  ഉത്തരം വേണം). Keyring prompt ഒരിക്കലും Scribus കാരണം വരില്ല.
- Keyring ഇല്ലെങ്കിൽ: login ഈ Scribus session-ൽ മാത്രം (memory-യിൽ; disk-ൽ ഒന്നുമില്ല). ഒരിക്കൽ message:
  "Login saved for this session only - keyring not available". Scribus വീണ്ടും തുറക്കുമ്പോൾ വീണ്ടും login.
- Login: 10 സെക്കൻഡ് പരമാവധി; Cancel എപ്പോഴും പ്രവർത്തിക്കും; വ്യക്തമായ error (server refused, server name not
  found, no answer within 10 seconds, wrong password …).
- ഓരോ login step-ഉം സമയത്തോടെ `~/scribus-crashlogs/login.log`-ൽ (password / token ഒരിക്കലും ഇല്ല).
- install.sh summary-യിൽ user-ന്റെ keyring check, ശരിയാക്കാനുള്ള വഴിയോടെ.

### 98. News Browser — "Page 2 is opened by …" (workflow Layout page-ലെ പോലെ)
- Page box-ൽ മറ്റൊരാൾ തുറന്ന page ചാരനിറത്തിൽ, പേരോടെ: "Page 2 – Suneer". List തുറക്കുമ്പോൾ വീണ്ടും server-നോട് ചോദിക്കും.
- അത്തരം page തിരഞ്ഞെടുത്താൽ (അല്ലെങ്കിൽ Fetch News): Page box-ന് താഴെ "Page 2 is opened by Suneer (since 09:12)",
  ചോദ്യം: [Continue anyway] [Choose another page].
- ഒഴിഞ്ഞ page തിരഞ്ഞെടുക്കുമ്പോൾ workflow-ൽ അത് നിങ്ങളുടെ പേരിൽ lock ആകും (web Layout page-ന്റെ അതേ lock).
  മറ്റൊരു page / date, edition മാറ്റം, document close, Logout, Scribus close — lock വിടും.
- Browser-ൽ നിങ്ങൾ തന്നെ മുമ്പേ lock ചെയ്ത page Scribus വിടില്ല.
- ഓരോ മിനിറ്റിലും check: admin unlock ചെയ്താൽ വീണ്ടും lock; മറ്റൊരാൾ എടുത്താൽ കാണിക്കും.
- Server ഉത്തരം തന്നില്ലെങ്കിൽ 5 സെക്കൻഡിൽ "Lock status … unknown" — Scribus ഒരിക്കലും നിൽക്കില്ല.

### 99. Box (Frame Border) — നാല് വശത്തും ഒരേ ഇടം, text-ന് അനുസരിച്ച് ഉയരം
- മുകളിലെ toolbar-ലെ border popup: "Inner space" (default 2 mm), "Fit box to text" (default on),
  "Linked / welded / grouped: Whole story / Each frame".
- ഒരു frame: നാല് വശത്തും ഒരേ inner space; മുകളിൽ അക്ഷരങ്ങൾക്ക് മുകളിൽ ആ ഇടം; അവസാനത്തെ ഒഴിഞ്ഞ വരികൾ നീക്കും;
  ഉയരം അവസാന വരിയിലെ അക്ഷരങ്ങളുടെ താഴെ + inner space. താഴെ ഇനി 2 വരി ഇടം വരില്ല.
- Welded / grouped / linked story: story മുഴുവൻ ഒരു box (frames-ന്റെ വലുപ്പം മാറില്ല, മറ്റൊന്നും നീങ്ങില്ല);
  story നീക്കുമ്പോൾ box-ഉം നീങ്ങും.
- Text edit ചെയ്ത് edit mode-ൽ നിന്ന് പുറത്തുവരുമ്പോൾ box വീണ്ടും fit ആകും — ഒരു Undo step.
- Justify ചെയ്ത text വലത് border-ൽ തൊടില്ല (അളവ്: 2.06 mm).

### 100. News Browser — "Open page from workflow"
- Fetch News-ന്റെ അടുത്ത് പുതിയ button. തിരഞ്ഞെടുത്ത Edition / Page / Date-ന്റെ .sla workflow-ൽ നിന്ന് download ചെയ്യും
  (workflow Layout editor-ലെ "Download SLA" തന്നെ: template + വെച്ച news + പടങ്ങൾ).
- Save ചെയ്യുന്നത്: `<pages folder>/<edition>/<date>/<page>.sla`. പടങ്ങൾ `<page>_images/`-ൽ.
  Pages folder Settings → Connection-ൽ മാറ്റാം (default `~/WorkflowPages`).
- File നേരത്തെ ഉണ്ടെങ്കിൽ ചോദിക്കും: Open existing / Replace / Keep both.
- തുറക്കുമ്പോൾ page lock എടുക്കും ("opened by"). Workflow ids document-ൽ സൂക്ഷിക്കും (SLA-യിൽ `SuneerWf*`).
  മറ്റൊരു page-ന്റെ news ഈ document-ൽ Place / Mark ചെയ്യുമ്പോൾ ആദ്യം ചോദിക്കും.
- Template ഇല്ലെങ്കിൽ അത് വ്യക്തമായി പറയും. Login, server error (401/403/404/500), timeout എന്നിവയുടെ സന്ദേശങ്ങൾ വ്യക്തം.

### 101. News Browser — page-ലെ വാർത്തകളും workflow-ലെ Used / Unused-ഉം ഒരുപോലെ (page sync)
- Workflow-ൽ നിന്ന് download ചെയ്ത page-ലെ frames-ൽ story id ഇല്ല. തുറക്കുമ്പോൾ workflow-ന്റെ layout rows (സ്ഥാനം) നോക്കി
  ഓരോ frame-നെയും അതിന്റെ വാർത്തയുമായി ബന്ധിപ്പിക്കും. Headline മാത്രം ഒക്കുന്നവ ചോദിച്ചിട്ടേ ബന്ധിപ്പിക്കൂ.
- Page തുറക്കുമ്പോഴും (workflow-ൽ നിന്നോ disk-ൽ നിന്നോ) Fetch News / Check ചെയ്യുമ്പോഴും:
  - page-ൽ ഉള്ള വാർത്ത → workflow-ൽ USED (ആ page-ന്).
  - USED എന്ന് കാണിക്കുന്നു, പക്ഷേ page-ൽ ഇല്ല → list കാണിക്കും: [Mark unused] [Keep]. സ്വയം ഒന്നും മാറ്റില്ല.
    Keep പറഞ്ഞത് ഓർക്കും — Scribus restart കഴിഞ്ഞും (settings `sync/kept`).
    Settings → Connection → "Page sync": എത്ര Keep ഉണ്ട് എന്ന് കാണാം; **Forget Keep...** എല്ലാം മറക്കും (workflow-ൽ ഒന്നും മാറ്റില്ല).
  - Disk-ൽ നിന്ന് workflow page തുറക്കുമ്പോൾ എപ്പോഴും "Sync now?" ചോദിക്കും (News Browser അതേ page-ൽ ആണെങ്കിലും).
  - id ഇല്ലാത്ത frames → list; headline ഒക്കുന്നവ [Link ticked].
- Place → USED, story-യുടെ അവസാന frame delete → UNUSED: download ചെയ്ത page-ലും ഇപ്പോൾ പ്രവർത്തിക്കും.
- Save-ലും close-ലും ബാക്കിയുള്ള marks വീണ്ടും അയയ്ക്കും (close-ൽ പരമാവധി 3 s). അയയ്ക്കാനായില്ലെങ്കിൽ
  `~/.cache/scribus/news/pending-marks.json`-ൽ സൂക്ഷിച്ച് അടുത്ത തവണ അയയ്ക്കും.
- Page lock ഉള്ള ആൾ മാത്രം ആ page-ന്റെ Used / Unused മാറ്റും. മറ്റൊരാൾ തുറന്ന page-ന് ഒന്നും അയയ്ക്കില്ല.
- Log: `~/scribus-crashlogs/news-sync.log` (token ഇല്ല).
- Fix: USED-ന് `fromStatus` അയച്ചിരുന്നു; workflow അത് 400 കൊണ്ട് നിരസിക്കും (undo-ക്ക് ശേഷമുള്ള USED പരാജയപ്പെട്ടിരുന്നു).
- Test: `tests/suneer/news_page_sync.sh <dir>` (mock workflow, രണ്ട് ഘട്ടം).

### 102. News Browser — മറ്റൊരാൾ തുറന്ന page "Take over page"
- മറ്റൊരാൾ lock ചെയ്ത page-ൽ (Continue anyway കഴിഞ്ഞ്) Page box-ന്റെ താഴെ **Take over page** button.
  ചോദിക്കും ("<ആൾ> will lose the lock"), പിന്നെ lock നിങ്ങളുടേതാകും, Used / Unused sync ഉടനെ നടക്കും.
- Workflow-ന്റെ **Locked Pages right** (`lockedPages.view`) ഉള്ളവർക്ക് മാത്രം (web-ലെ Locked Pages പോലെ).
- ഒരേ account (മറ്റൊരു PC): ഇപ്പോഴും "നിങ്ങളുടേത്" — ചോദ്യമില്ല, sync തടയില്ല.
- **Details…**: owner, since, PC, last heartbeat, sync skip-ന്റെ കാരണം, take over പറ്റുമോ.
  Workflow ഇപ്പോൾ PC / heartbeat സൂക്ഷിക്കുന്നില്ല → "not recorded by the workflow".
- Workflow-ൽ heartbeat വന്നാൽ (spec 2026-10-30): Scribus ഓരോ മിനിറ്റിലും heartbeat അയയ്ക്കും;
  3 മിനിറ്റ് heartbeat ഇല്ലാത്ത lock "stale" — right ഇല്ലാത്തവർക്കും take over.
- Test: `tests/suneer/news_page_takeover.sh <dir>` (mock).

### 103. Workflow-ൽ നിന്ന് തുറന്ന obituary page-ലെ ഫോട്ടോകൾ — "Process obituary photos"
- Workflow download-ലെ obituary ഫോട്ടോകൾക്ക് background removal ഇല്ല. ഇപ്പോൾ News Browser placement-ന്റെ
  അതേ pipeline (BG removal, face crop/centre, Obit BG colour, tone) ചെയ്യാം:
  - News Browser-ൽ **Process obituary photos** button (select ചെയ്തവ, ഒന്നും select ചെയ്തില്ലെങ്കിൽ page മുഴുവൻ).
  - Right-click: page-ൽ "Process obituary photos (page)", ഫോട്ടോയിൽ "Process obituary photos (N selected)".
  - Workflow-ൽ നിന്ന് obituary page തുറക്കുമ്പോൾ: "12 photos need background removal – process now?"
    (page + date-ന് ഒരിക്കൽ മാത്രം ചോദിക്കും; Settings → Obituary-ൽ off ആക്കാം, default ON).
- Background-ൽ ഓടും (progress bar, **Cancel**, Scribus ഉപയോഗിക്കാം). Original `<sla>_photos/original`-ൽ,
  ഫലം `<sla>_photos/edited/obit`-ൽ. Frame-ന്റെ വലുപ്പം, caption, weld മാറില്ല. Batch മുഴുവൻ ഒരു undo.
- Edit photo കൊണ്ട് കൈകൊണ്ട് മാറ്റിയവ, നേരത്തെ process ചെയ്തവ ഒഴിവാക്കും. Model / face data ഇല്ലെങ്കിൽ original
  നിലനിർത്തി കാരണം പറയും. Page lock ഉള്ള ആൾ മാത്രം. Processed / failed / skipped എണ്ണവും കാരണവും Details-ൽ.
- Fix: **Re-process photos**-ന്റെ undo പഴയ ഫോട്ടോ തിരികെ കൊണ്ടുവന്നിരുന്നില്ല. ഇപ്പോൾ പുതിയ file (`_r1.png`…)
  എഴുതും, undo പഴയത് തിരികെ കൊണ്ടുവരും.
- Fix: ആദ്യ rembg model ഇല്ലെങ്കിൽ എല്ലാ ഫോട്ടോയും fail ആയിരുന്നു, മറ്റ് models നോക്കാതെ. ഇപ്പോൾ അടുത്ത model നോക്കും.
- Test: `tests/suneer/news_obit_wf_photos.sh <dir>` (mock).

### 104. "Process obituary photos" ഒന്നും ചെയ്യാതിരുന്നത് — തിരുത്തി
- കാരണം: page തുറന്ന ഉടനെയുള്ള save-ൽ "Embed all and continue" (default) എല്ലാ ഫോട്ടോയും embed ചെയ്തു;
  ഫയൽ പേര് നോക്കിയുള്ള തിരിച്ചറിയൽ പിന്നെ ഒന്നും കണ്ടില്ല; ആ വഴി ഒരു സന്ദേശവും log-ഉം ഇല്ലാതെ നിന്നു.
- ഇപ്പോൾ: obituary ഫോട്ടോ = workflow headshot (`_obit_`, embed ചെയ്താലും) അല്ലെങ്കിൽ story obituary ആണെങ്കിൽ
  (placement-ന്റെ അതേ obituary/news തീരുമാനം). News story-യുടെ ഫോട്ടോ തൊടില്ല.
- Embed ചെയ്ത ഫോട്ടോ: original പുറത്തെടുത്ത് process ചെയ്ത് link ചെയ്യും; undo വീണ്ടും embed ആക്കും.
- Save ചെയ്യാത്ത page: എവിടെ save ചെയ്യണം എന്ന് ചോദിക്കും (pages folder default).
- എപ്പോഴും ഫലം കാണിക്കും (box + status + Details) — "0 processed – കാരണം". ഓരോ frame-ന്റെയും തീരുമാനം log-ൽ.
- Workflow page തുറന്നശേഷമുള്ള auto save ഇനി "embed?" ചോദിക്കില്ല; ചോദ്യം sync കഴിഞ്ഞ് വരും.
- Face crop: ചെറിയ, ഉറപ്പില്ലാത്ത (eyes ഇല്ല, ഒരു cascade മാത്രം) box face ആയി എടുക്കില്ല (ഒരു കണ്ണിലേക്ക് zoom ചെയ്തിരുന്നു).

### 105. Edit photo / obituary photo: frame resize ചെയ്താൽ original തിരികെ വന്നിരുന്നു — തിരുത്തി
- Edit photo → Apply (BG removed), അല്ലെങ്കിൽ obituary pipeline ചെയ്ത ഫോട്ടോ: frame resize (handle + auto-resample),
  image size toolbar W/H/dpi, Resize Image, Optimize images — എല്ലാം ഇപ്പോൾ **ഇപ്പോഴത്തെ ഫോട്ടോ** (edit-ന്റെ ഫലം) resample ചെയ്യും.
  മുമ്പ് `SuneerImgOriginal` (edit-ന് മുമ്പുള്ള original) എടുത്തിരുന്നു → BG removal പോയിരുന്നു (save ചെയ്താൽ സ്ഥിരമായി).
- Original ഉപയോഗിക്കുന്നത് നിങ്ങൾ പറയുമ്പോൾ മാത്രം (Edit photo, Reset to original). Edit photo ഇപ്പോഴും original + settings തുറക്കും.
- ഒരേ ചിത്രം വീണ്ടും വീണ്ടും resize ചെയ്താൽ quality കുറയാതിരിക്കാൻ: resample എപ്പോഴും ആ ചിത്രത്തിന്റെ full-size file-ൽ നിന്ന്
  (`SuneerResampleOf/Out`; പഴയ documents-ൽ resample file-ന്റെ പേരിൽ നിന്ന്).
- Test: `tests/suneer/image_keep_edit.sh <dir>` (pixel നോക്കി; embedded, save/reopen ഉൾപ്പെടെ).

### 106. പഴയ resize-ൽ നഷ്ടപ്പെട്ട photo edits — document തുറക്കുമ്പോൾ സ്വയം തിരികെ
- #105-ന് മുമ്പുള്ള build-ൽ resize കാരണം original തിരികെ വന്ന ഫോട്ടോകൾ (edit / obituary processed file ഇപ്പോഴും ഉണ്ട്,
  പക്ഷേ frame original-ന്റെ resample copy കാണിക്കുന്നു) — document തുറക്കുമ്പോൾ edit-ലേക്ക് തിരികെ (frame-ൽ fit, embed
  ആയിരുന്നെങ്കിൽ വീണ്ടും embed). ഒരു undo step; box + status bar + `~/scribus-crashlogs/photo-restore.log`. Save ചെയ്താൽ സ്ഥിരം.
- "Reset to original" ചെയ്തതും edit-ന്റെ resample-ഉം തൊടില്ല. Edit file ഇല്ലെങ്കിൽ കാരണം പറയും.
- Fix: load സമയത്ത് ഉണ്ടാക്കിയ undo step Ctrl+Z-ൽ crash ആക്കിയിരുന്നു (Undo widget); restore ഇപ്പോൾ load കഴിഞ്ഞ്,
  Undo widget ശൂന്യമായ list-ൽ crash ആകില്ല.

### 107. ഒരേ വാർത്ത / ഒരേ ഫോട്ടോ രണ്ടുതവണ — കണ്ടെത്തൽ
- News Browser list-ൽ **DUP** badge: list-ലെ മറ്റൊരു വാർത്തയുടെയോ page-ൽ ഉള്ളതിന്റെയോ ആവർത്തനം (text ≥ 85 %, Malayalam
  normalise: spaces, punctuation, ZWJ/ZWNJ, soft hyphen), ഒരേ ഫോട്ടോ (resize / recompress / ~10 % crop ചെയ്താലും), workflow
  പ്രകാരം മറ്റൊരു page-ൽ used. Tooltip: "Same as: <headline> (92 %)". "Show: no DUP" ആവർത്തനങ്ങൾ മറയ്ക്കും.
- Obituary: ഒരേ പേര് മാത്രം പോരാ; പ്രായം വ്യത്യസ്തമെങ്കിൽ രണ്ട് ആളുകൾ.
- Place ചെയ്യുമ്പോൾ: "This story / photo is already on the page (module X)" [Place anyway] [Skip] [Show].
- News Browser-ൽ നിന്ന് page model slot-ലേക്ക് drag and drop ചെയ്യുമ്പോഴും ഇതേ മുന്നറിയിപ്പ്.
- "Check duplicates on page" (News Browser button, page right-click): എല്ലാ ജോഡികളും, click → അവിടേക്ക്.
- Photo hash (pHash) `<sla>_photos/hashes.json`-ൽ cache; BG removed / edit ചെയ്ത ഫോട്ടോ original-ന്റെ hash.
- Settings → Layout: "Duplicate news at" (85 %), "Same photo within" (6 bits). Background-ൽ (154 stories ~0.5 s).
- Test: `tests/suneer/news_duplicates.sh <obituary page.sla> <dir>`.

### 108. Check Repetition on Page — ഒരേ വാർത്ത, ഒരേ ഫോട്ടോ, ആവർത്തിച്ച വാചകം / വാക്ക്
- **Layout → Check Repetition on Page…**, Edit toolbar button, page right-click, News Browser-ലെ button, **Ctrl+Alt+D**.
  ഈ page മാത്രം (panel-ൽ "Whole document" തിരഞ്ഞെടുക്കാം).
- കണ്ടെത്തുന്നത്: ഒരേ story id രണ്ടുതവണ; മറ്റൊരു id-ൽ ഏകദേശം ഒരേ വാർത്ത (Malayalam normalise, മുഴുവൻ text-ഉം ≥ 85 %);
  ഒരേ ഫോട്ടോ (resize / recompress / ചെറിയ crop / BG removed); ഒരു story-യിൽ paragraph / വാചകം ആവർത്തിച്ചത്;
  രണ്ട് story-കളിൽ ഒരേ നീണ്ട വാചകം; "അന്തരിച്ചു അന്തരിച്ചു" പോലെ വാക്ക് രണ്ടുതവണ.
- Entry click → രണ്ടും select, zoom, മഞ്ഞ mark (screen-ൽ മാത്രം, print / PDF-ൽ ഇല്ല). [Ignore] [Delete this one] — ഒരു Delete = ഒരു undo.
- Pasteboard, hidden / non-printing layer, ചെറിയ icon / strip (< 14 mm) നോക്കില്ല.
- തിരുത്തൽ: CMYK JPEG ഉള്ള page-ൽ photo hash Scribus crash ചെയ്തിരുന്നു (#107 DUP badge-ഉം) — ഇനി ഇല്ല.
- തിരുത്തൽ: ഒരേ വാർത്ത രണ്ടുതവണ place ചെയ്ത് ഒന്ന് delete ചെയ്താൽ workflow-ൽ Unused ആയിരുന്നു — ഇനി മറ്റേത് ഉണ്ടെങ്കിൽ ഇല്ല.
- ഫോട്ടോ: 6 bits-ന് ഉള്ളിൽ + thumbnail correlation ≥ 0.95 (രണ്ട് വ്യത്യസ്ത സ്ത്രീകളുടെ ഫോട്ടോ 6 bits ആയിരുന്നു).
- Test: `tests/suneer/repeat_on_page.sh <page.sla> <dir>`; `SUNEER_REPEAT_MODE=scan` = false positive scan.

### 109. Frame text-ന് fit ചെയ്യുമ്പോൾ — മുകളിലും താഴെയും ഒരേ ഇടം
- എല്ലാ fit-ഉം ഒരേ രീതിയിൽ: Item → Adjust Frame Height to Text (താഴത്തെ handle double-click ഉൾപ്പെടെ), toolbar Fit,
  Styled Paste-ന് ശേഷമുള്ള auto-fit, News Browser placement, Layout Models / obituary modules / Re-pack, Frame Border
  "Fit box to text", Fit Caption Frame.
- താഴെ ഇടം = അവസാന വരിയിലെ അക്ഷരങ്ങളുടെ യഥാർത്ഥ അടിഭാഗം മുതൽ; മുകളിലെ ഇടം (top inset + first line offset) പോലെ തന്നെ.
  Malayalam / English, 8–18 pt, Fixed / Auto: വ്യത്യാസം 0.02–0.14 mm. Overflow ഇല്ല (0.1 pt അധികം).
- Fit ചെയ്യുമ്പോൾ story-യുടെ അവസാനത്തെ ശൂന്യ paragraphs നീക്കും. Text frame-ന്റെ അവസാനത്തിൽ paste ചെയ്യുമ്പോഴും (ഒരേ undo).
- Bottom text distance: ആവശ്യമെങ്കിൽ മാത്രം കുറയ്ക്കും (font descent-ന് വേണ്ടത്ര).
- തിരുത്തൽ: toolbar Fit bottom distance രണ്ടുതവണ + 2 pt ചേർത്തിരുന്നു; menu Adjust Height ചെറിയ frame-ൽ overflow വിട്ടിരുന്നു.
- Test: `tests/suneer/fit_gap.sh <dir>` (FITGAP_TEXT=clean: ശൂന്യ paragraph ഇല്ലാതെ).

### 110. Fit to text — പല columns ഉള്ള frame-ഉം linked chain-ഉം
- Columns > 1: text എല്ലാ columns-ലും കൃത്യം നിറയുന്ന ഏറ്റവും ചെറിയ ഉയരം (10 / 10 / 10 വരികൾ; അവസാന column അല്പം ചെറുതാകാം),
  താഴെ ഇടം = മുകളിലെ ഇടം (#109 പോലെ). മുമ്പ്: ആദ്യ column-ന്റെ ഉയരം (14 / 11 / 5).
- Settings → Layout: **"Balance columns when fitting"** (default on). Off = പഴയ പോലെ ആദ്യ column-ന് fit.
- Linked chain: അവസാന frame fit ചെയ്യും, മുമ്പത്തെ frames മാറില്ല.
- Keep lines / widows / baseline grid: layout തന്നെ പാലിക്കുന്നു. Overflow ഒരിക്കലും ഇല്ല (0.1 pt ഉയരം + bottom distance-ൽ 0.3 pt margin — reload-ൽ ഒരു വരി നഷ്ടപ്പെട്ടിരുന്നു).
- എല്ലാ fit വഴികളും (toolbar, Adjust Frame Height, Frame Border, paste, placement, obituary 2-col).
- **ഓരോ column-നും അതിന്റെ പങ്ക്:** fit `SuneerColumnLines` (item attribute, SLA-ൽ save) വെക്കും = ceil(വരികൾ / columns); layout ആ എണ്ണം
  കഴിഞ്ഞാൽ അടുത്ത column-ലേക്ക് (36 വരി 5 column: 8/7/7/7/7, greedy 8/8/8/8/4 അല്ല). Cap-ഓടെ overflow വന്നാൽ (text മാറ്റി, columns
  മാറ്റി) layout സ്വയം greedy ആയി വീണ്ടും — text ഒരിക്കലും മറയില്ല.
- Running app-ൽ യഥാർത്ഥ commands (menu, toolbar, Styled Paste) കൊണ്ട് test: `FITGAP_TEXT=app tests/suneer/fit_gap.sh <dir>` (FIT-TRACE വരികൾ).
- Test: `FITGAP_TEXT=columns tests/suneer/fit_gap.sh <dir>`.
- Laptop test package ഇനി `<date>-<N>.local<M>` (N = അന്നത്തെ release): അന്നത്തെ release-ന് മുകളിൽ — updater `0.local1`-നെ release കൊണ്ട് മാറ്റിയിരുന്നു.
- Styled Paste (frame select ചെയ്ത് paste): target frame-ന്റെ columns / gap നിലനിൽക്കും (മുമ്പ് source-ന്റേത് വന്നു).

### 111. Multi-column fit — newspaper rule: മുമ്പത്തെ column പിന്നത്തേതിനേക്കാൾ മുകളിൽ അവസാനിക്കില്ല
- Column k-യുടെ അവസാന വരി column k+1-ന്റേതിന് താഴെയോ ഒരേ നിരയിലോ (എല്ലാ k-യ്ക്കും); frame bottom = column 1-ന്റെ അവസാന വരി + ഇടം.
- Span heading (columns-ന് മുകളിൽ നീളുന്ന തലക്കെട്ട്) വരികൾ column-ന്റെ എണ്ണത്തിൽ ഇല്ല (മുമ്പ് column 1-ന്റെ പങ്കിൽ കൂടി → column 1 ഒരു വരി ചെറുത്).
- `SuneerColumnLines` ഇനി ഓരോ column-ന്റെയും എണ്ണം ("18,17"); ബാക്കി വരികൾ ആദ്യ columns-ന്. ചെറിയ dateline / paragraph gap കാരണം പിന്നത്തെ column താഴെ എത്തിയാൽ ഒരു വരി മുന്നോട്ട്.
- Test: `SUNEER_FITGAP_MODE=doc SUNEER_FITGAP_COLS=N` (യഥാർത്ഥ page), `fit_gap.sh` rule `LATER-COLUMN-LOWER`.

### 112. Updates — manual മാത്രം (automatic check ഇല്ല)
- Scribus തുറക്കുമ്പോൾ update check ഇല്ല; timer ഇല്ല; background check ഇല്ല; "പുതിയ version" popup ഇല്ല.
- Help → **Check for Updates…** തുറക്കുമ്പോൾ മാത്രം server-ൽ നോക്കും (installed / latest / what's new); **Update** അമർത്തിയാൽ മാത്രം install (pkexec, admin password).
- "Check automatically" എന്ന option ഇല്ല (ഒരിക്കലും ഉണ്ടായിരുന്നില്ല) — manual only എന്നത് സ്ഥിരം; prefs-ൽ ഒന്നുമില്ല, install.sh-ൽ മാറ്റമില്ല.
- `scribus-housekeeping.timer` (systemd user, daily) crash log prune മാത്രം; update-മായി ബന്ധമില്ല.
- പഴയ builds (≤ 20261031-2) ഈ build കിട്ടുന്നത് വരെ ദിവസം ഒരിക്കൽ ചോദിക്കും — ഒരിക്കൽ കൂടി.
- Test: `tests/suneer/update_manual_only.sh <dir>` (local mock server, newer signed test manifest).
