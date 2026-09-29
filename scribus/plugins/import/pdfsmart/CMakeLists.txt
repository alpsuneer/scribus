include_directories(
	${CMAKE_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/scribus
	${poppler_INCLUDE_DIR}
	/usr/include/poppler
	${poppler_cpp_INCLUDE_DIR}
	${SCRIBUS_AUTOGEN_INCLUDE_PATH}
)

# Session 2: pdfsmarttextextractor.cpp now uses poppler-cpp for real, so this
# plugin is only built when poppler is available -- see the matching guard
# around add_subdirectory(pdfsmart) in ../CMakeLists.txt.
#
# Session 3: pdfsmartgraphicsextractor.cpp additionally uses poppler's
# low-level (non-cpp) API -- GfxState/OutputDev/PDFDoc, the same headers
# plugins/import/pdf/slaoutput.cpp uses -- so it needs the same extra
# defines that plugin's CMakeLists.txt sets before including those headers,
# in case they affect struct layout.
if(HAVE_LCMS1)
	add_definitions(-DUSE_CMS)
endif()
add_definitions(-DHAVE_FREETYPE_H)

set(IMPORTPDFSMART_PLUGIN_SOURCES
	importpdfsmart.cpp
	importpdfsmartplugin.cpp
	pdfsmartimportoptions.cpp
	pdfsmarttextextractor.cpp
	pdfsmartgraphicsextractor.cpp
	pdfsmartimageextractor.cpp
	pdfsmartcolumndetector.cpp
	pdfsmartfontextractor.cpp
	pdfsmartfontmapper.cpp
	pdfsmartmalayalamfix.cpp
)

set(SCRIBUS_IMPORTPDFSMART_PLUGIN "importpdfsmart")

add_library(${SCRIBUS_IMPORTPDFSMART_PLUGIN} MODULE ${IMPORTPDFSMART_PLUGIN_SOURCES})

link_directories( /usr/local/lib )
# poppler_LIBRARY is libpoppler.so (the low-level GfxState/OutputDev/PDFDoc
# API pdfsmartgraphicsextractor.cpp uses). poppler_cpp_LIBRARY is the
# SEPARATE libpoppler-cpp.so (the poppler::document/page API
# pdfsmarttextextractor.cpp and pdfsmartimageextractor.cpp use) -- both are
# defined by cmake/modules/Findpoppler.cmake's two independent
# find_library() calls, and both are real, separate shared objects on
# disk (confirmed: ldd on the built plugin showed only libpoppler.so linked
# before this fix, and calling poppler::document::load_from_file() -- a
# poppler-cpp symbol -- crashed with "undefined symbol" at plugin load).
#
# Session 7 (Feature 7, embedded fonts) needed no addition here: font
# registration goes through SCFonts::addScalableFonts() (public; the
# single-file addScalableFont() is private), which manages its own
# FT_Library internally, so this plugin never calls FreeType directly and
# doesn't need to link it.
target_link_libraries(${SCRIBUS_IMPORTPDFSMART_PLUGIN} ${EXE_NAME} ${poppler_LIBRARY} ${poppler_cpp_LIBRARY})

if(WANT_PCH)
	target_precompile_headers(${SCRIBUS_IMPORTPDFSMART_PLUGIN} PRIVATE "../../plugins_pch.h")
endif()

install(TARGETS ${SCRIBUS_IMPORTPDFSMART_PLUGIN}
	LIBRARY
	DESTINATION ${PLUGINDIR}
	PERMISSIONS ${PLUGIN_PERMISSIONS}
)

add_dependencies(Plugins ${SCRIBUS_IMPORTPDFSMART_PLUGIN})
