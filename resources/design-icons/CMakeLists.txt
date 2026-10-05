# Design Style icons shipped with every package. SuneerDesignStyle.conf names
# an icon by file name; the panel looks it up in the user's
# ~/.local/share/scribus/design-icons/ first and then here.
file(GLOB DESIGN_ICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.png")

install(FILES
${DESIGN_ICON_FILES}
DESTINATION ${SHAREDIR}design-icons
)
