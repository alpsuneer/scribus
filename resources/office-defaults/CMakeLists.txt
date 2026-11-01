# Office default settings shipped with every package (tools/update-office-defaults.sh writes them
# from the release laptop; SuneerOfficeDefaults syncs them into each user profile at start).
# office-defaults.json: version + sha256 of each file and of every earlier shipped copy.
file(GLOB OFFICE_DEFAULT_FILES
	"${CMAKE_CURRENT_SOURCE_DIR}/SuneerDesignStyle.conf"
	"${CMAKE_CURRENT_SOURCE_DIR}/ParagraphStyleShortcuts.conf"
	"${CMAKE_CURRENT_SOURCE_DIR}/suneer-text-gradients.json"
	"${CMAKE_CURRENT_SOURCE_DIR}/office-defaults.json"
)
install(FILES ${OFFICE_DEFAULT_FILES} DESTINATION ${SHAREDIR}office-defaults)
