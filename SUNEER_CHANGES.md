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

### 15. Authors
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
