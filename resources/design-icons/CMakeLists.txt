# Design Style icons shipped with every package. SuneerDesignStyle.conf names
# an icon by file name; the panel looks it up in the user's
# ~/.local/share/scribus/design-icons/ first and then here.
# Exactly these ten, listed by name: a stray PNG in this folder is not shipped.
# double.png added 2026-11-01 (design style 10, "Style 10", Ctrl+F10).
install(FILES
	single.png
	style-1.png
	style-2.png
	style-3.png
	style-4.png
	style-1photo.png
	style-2photo.png
	style-3photo.png
	style-4photo.png
	double.png
	DESTINATION ${SHAREDIR}design-icons
)
