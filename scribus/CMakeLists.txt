#For those that want it, show the SVN version in the About dialog
if (WANT_SVNVERSION)
	add_definitions(-DHAVE_SVNVERSION)
	message(STATUS "SVN Version requested in About dialog")
	# a custom target that is always built

	if (NOT CMAKE_VERSION VERSION_LESS "3.2.0")
		add_custom_target(svnheader ALL COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/modules/getsvn.cmake BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/svnversion.h)
	else ()
		add_custom_target(svnheader ALL DEPENDS svn_header ) # svn_header is nothing more than a unique string
		# creates svnheader.h using cmake script
		add_custom_command(OUTPUT svn_header ${CMAKE_CURRENT_BINARY_DIR}/svnversion.h
		COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_MODULE_PATH}/getsvn.cmake)
		set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/svnversion.h
			PROPERTIES GENERATED TRUE
			HEADER_FILE_ONLY TRUE)
	endif ()
endif ()

if(WIN32)
	add_definitions(-DQT_DLL)
endif()


include_directories(
	${CMAKE_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}
	${FREETYPE_INCLUDE_DIRS}
	${HARFBUZZ_INCLUDE_DIRS}
	${HARFBUZZ_SUBSET_INCLUDE_DIRS}
	${hunspell_INCLUDE_DIRS}
)

include(CMakeLists_Sources.txt)
include(api/CMakeLists.txt)
include(colormgmt/CMakeLists.txt)
include(desaxe/CMakeLists.txt)
include(fonts/CMakeLists.txt)
include(styles/CMakeLists.txt)
include(text/CMakeLists.txt)

add_subdirectory(dtd)
# add_subdirectory(ui/qml)
if(WITH_TESTS)
	add_subdirectory(tests)
endif()
if (WANT_DESIGNER)
	add_subdirectory(designer)
endif ()
add_subdirectory(third_party)





set(CMAKE_AUTOUIC_SEARCH_PATHS ui/)

set_property(SOURCE third_party/fparser/file.hh PROPERTY SKIP_AUTOGEN ON)
set_property(SOURCE third_party/fparser/fparser.hh PROPERTY SKIP_AUTOGEN ON)

#if(WANT_QTADS)
  MESSAGE(STATUS "Qt Advanced Docking System included")
  link_directories(${CMAKE_CURRENT_BINARY_DIR}/third_party/Qt-Advanced-Docking-System)
#endif()

link_directories(
	${CMAKE_CURRENT_BINARY_DIR}/third_party/wpg
	${CMAKE_CURRENT_BINARY_DIR}/third_party/pgf
	${CMAKE_CURRENT_BINARY_DIR}/third_party/rtf-qt
	${CMAKE_CURRENT_BINARY_DIR}/third_party/zip
	${HARFBUZZ_LIBRARY_DIRS}
	${HARFBUZZ_SUBSET_LIBRARY_DIRS}
	${hunspell_LIBRARY_DIRS}
	${ICU_LIBRARY_DIRS}
)

if(WITH_TESTS)
	link_directories( ${CMAKE_CURRENT_BINARY_DIR}/tests )
	set(SCRIBUS_TESTS_LIB "scribus_tests_lib")
else()
	set(SCRIBUS_TESTS_LIB "")
endif()

if(HAVE_OSG)
	link_directories( ${CMAKE_CURRENT_BINARY_DIR}/third_party/prc )
	set(SCRIBUS_PRC_LIB "scribus_prc_lib")
else()
	set(SCRIBUS_PRC_LIB "")
endif()

if(HAVE_BOOST)
	link_directories( ${CMAKE_CURRENT_BINARY_DIR}/third_party/lib2geom )
	set(SCRIBUS_2GEOM_LIB "scribus_2geom_lib")
else()
	set(SCRIBUS_2GEOM_LIB "")
endif()

set(SCRIBUS_WPG_LIB "scribus_wpg_lib")
set(SCRIBUS_PGF_LIB "scribus_pgf_lib")
set(SCRIBUS_ZIP_LIB "scribus_zip_lib")
set(SCRIBUS_RTF_LIB "scribus_rtf_lib")

if (GMAGICK_FOUND)
	message(STATUS "GMAGICK: Libs: ${GMAGICK_LDFLAGS}, Inc: ${GMAGICK_INCLUDE_DIRS}, Ldflags: ${GMAGICK_LDFLAGS}")
	include_directories(
		BEFORE ${GMAGICK_INCLUDE_DIRS}
	)
	link_directories(
		${GMAGICK_LIBRARY}
	)
endif()

if(HAVE_PODOFO)
	if(OPENSSL_FOUND)
		message(STATUS "Including OpenSSL files for PoDoFo >= 0.9.5")
		include_directories(${OPENSSL_INCLUDE_DIR})
	else()
#		message(STATUS "Not including OpenSSL files because this system's PoDoFo does not require them")
	endif()
	include_directories(${LIBPODOFO_INCLUDE_DIR})

endif()

if(HAVE_HYPHEN)
	target_link_libraries(${EXE_NAME} PRIVATE ${HYPHEN_LIBRARY})
endif()


if(GESTURE_FRAME_PREVIEW)
	 message(STATUS "Enable gesture frame preview [experimental]")
	 set(GESTURE_FRAME_PREVIEW_SOURCES	pageitempreview.cpp)
	 add_definitions(-DGESTURE_FRAME_PREVIEW)
endif()

if(APPLEBUNDLE)
	set(EXE_NAME_PREFIX "Scribus")
else()
	set(EXE_NAME_PREFIX "scribus")
endif()
set(EXE_NAME "${EXE_NAME_PREFIX}${TAG_VERSION}" CACHE STRING "Scribus version")

if (NOT DEFINED CMAKE_GLOBAL_AUTOGEN_TARGET_NAME)
	set(SCRIBUS_AUTOGEN_INCLUDE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}_autogen/include")
else()
	set(SCRIBUS_AUTOGEN_INCLUDE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}_${CMAKE_GLOBAL_AUTOGEN_TARGET_NAME}/include")
endif()
message(STATUS "Autogen Include Path: ${SCRIBUS_AUTOGEN_INCLUDE_PATH}")

if(HAVE_OSG)
	add_executable(${EXE_NAME}
		${SCRIBUS_MAIN_CPP}
		${SCRIBUS_SOURCES}
		ui/ParagraphStylesPanel.cpp
		ui/scshortcutregistry.cpp
		ui/suneerlinkedimagesdialog.cpp
		suneerimagelinks.cpp
		ui/faircodehelpviewer.cpp
		ui/suneercontrolbar.cpp
		util/suneeralphawrap.cpp
		ui/suneer_news_panel.cpp
		ui/suneerduplicatenewsdialog.cpp
		suneerduplicatenews.cpp
		suneerfilltextimage.cpp
		suneerposterstack.cpp
		suneerpopout.cpp
		suneertexteffects.cpp
		ui/scimageeditor.cpp
		ui/scimagefilterdialogs.cpp
		ui/optionsbar.cpp
		ui/imageeraseroptions.cpp
		ui/removaltoolwidget.cpp
		ui/aitextresultdialog.cpp
		ui/aitranslatedialog.cpp
		ui/inpaintprogressdialog.cpp
		ui/contourdetectdialog.cpp
		ui/frameshapemenu.cpp
		ui/tooloptions/moveoptionswidget.cpp
		ui/tooloptions/cropoptionswidget.cpp
		ui/dialogs/imagesizedialog.cpp
		ui/dialogs/canvassizedialog.cpp
		ui/dialogs/autoarrangedialog.cpp
		ui/proofprintdialog.cpp
		ui/marchingantsitem.cpp
		ui/tools/imagetool.cpp
		ui/tools/rectmarqueetool.cpp
		ui/tools/ellipsemarqueetool.cpp
		ui/tools/lassotool.cpp
		ui/tools/polygonlassotool.cpp
		ui/tools/penpathtool.cpp
		ui/tools/samselecttool.cpp
		ui/tools/refineedgesbrushtool.cpp
		ui/dialogs/autoenhancedialog.cpp
		scimagefilterengine.cpp
		scimagehistogram.cpp
		scimageselection.cpp
		scimagesam.cpp
		autocorrectengine.cpp
		autoarrangeengine.cpp
	singleinstance.cpp
	documentlock.cpp
	pdfpresets.cpp
	netpathguard.cpp
		canvasmode_suneercontour.cpp
		canvasmode_suneercrop.cpp
		canvasmode_shapecrop.cpp
		util_shapecrop.cpp
		canvasmode_imageeraser.cpp
		canvasmode_removalmask.cpp
		ai/aiinpaintservice.cpp
		ai/aiinpaintservicefactory.cpp
		ai/aitextservice.cpp
		ai/aitextprompts.cpp
		ai/aitexthttpservice.cpp
		ai/aitextservicefactory.cpp
		ai/claudetextservice.cpp
		ai/geminiinpaintservice.cpp
		ai/geminitextservice.cpp
		ai/lamainpaintservice.cpp
		ai/openaitextservice.cpp
		ai/openrouterinpaintservice.cpp
		scimageerasermask.cpp
		util_contour.cpp
		util_inpaint.cpp
		scupdateclient.cpp
		scupdatekeystore.cpp
		ui/updatesettingsdialog.cpp
		ui/updateavailabledialog.cpp
		ui/updatecheckdialog.cpp
		scimpositionengine.cpp
		ui/impositiondialog.cpp
		ui/impositionpreviewwidget.cpp
		ui/tile_preview_widget.cpp
		offset_separation_presets.cpp
		offset_tiling.cpp
		ui/pdfseparationsviewer.cpp
		util_pdfsep.cpp
		${SCRIBUS_API_SOURCES}
		${SCRIBUS_COLORMGMT_SOURCES}
		${SCRIBUS_DESAXE_SOURCES}
		${SCRIBUS_FONTS_SOURCES}
		${SCRIBUS_STYLES_SOURCES}
		${SCRIBUS_TEXT_SOURCES}
		${SCRIBUS_WIN32_ONLY_SOURCES}
		${GESTURE_FRAME_PREVIEW_SOURCES}
		${SCRIBUS_PRC_SOURCES}
	)
else()
	add_executable(${EXE_NAME}
		${SCRIBUS_MAIN_CPP}
		${SCRIBUS_SOURCES}
		ui/ParagraphStylesPanel.cpp
		ui/scshortcutregistry.cpp
		ui/suneerlinkedimagesdialog.cpp
		suneerimagelinks.cpp
		ui/faircodehelpviewer.cpp
		ui/suneercontrolbar.cpp
		util/suneeralphawrap.cpp
		ui/suneer_news_panel.cpp
		ui/suneerduplicatenewsdialog.cpp
		suneerduplicatenews.cpp
		suneerfilltextimage.cpp
		suneerposterstack.cpp
		suneerpopout.cpp
		suneertexteffects.cpp
		ui/scimageeditor.cpp
		ui/scimagefilterdialogs.cpp
		ui/optionsbar.cpp
		ui/imageeraseroptions.cpp
		ui/removaltoolwidget.cpp
		ui/aitextresultdialog.cpp
		ui/aitranslatedialog.cpp
		ui/inpaintprogressdialog.cpp
		ui/contourdetectdialog.cpp
		ui/frameshapemenu.cpp
		ui/tooloptions/moveoptionswidget.cpp
		ui/tooloptions/cropoptionswidget.cpp
		ui/dialogs/imagesizedialog.cpp
		ui/dialogs/canvassizedialog.cpp
		ui/dialogs/autoarrangedialog.cpp
		ui/proofprintdialog.cpp
		ui/marchingantsitem.cpp
		ui/tools/imagetool.cpp
		ui/tools/rectmarqueetool.cpp
		ui/tools/ellipsemarqueetool.cpp
		ui/tools/lassotool.cpp
		ui/tools/polygonlassotool.cpp
		ui/tools/penpathtool.cpp
		ui/tools/samselecttool.cpp
		ui/tools/refineedgesbrushtool.cpp
		ui/dialogs/autoenhancedialog.cpp
		scimagefilterengine.cpp
		scimagehistogram.cpp
		scimageselection.cpp
		scimagesam.cpp
		autocorrectengine.cpp
		autoarrangeengine.cpp
	singleinstance.cpp
	documentlock.cpp
	pdfpresets.cpp
	netpathguard.cpp
		canvasmode_suneercontour.cpp
		canvasmode_suneercrop.cpp
		canvasmode_shapecrop.cpp
		util_shapecrop.cpp
		canvasmode_imageeraser.cpp
		canvasmode_removalmask.cpp
		ai/aiinpaintservice.cpp
		ai/aiinpaintservicefactory.cpp
		ai/aitextservice.cpp
		ai/aitextprompts.cpp
		ai/aitexthttpservice.cpp
		ai/aitextservicefactory.cpp
		ai/claudetextservice.cpp
		ai/geminiinpaintservice.cpp
		ai/geminitextservice.cpp
		ai/lamainpaintservice.cpp
		ai/openaitextservice.cpp
		ai/openrouterinpaintservice.cpp
		scimageerasermask.cpp
		util_contour.cpp
		util_inpaint.cpp
		scupdateclient.cpp
		scupdatekeystore.cpp
		ui/updatesettingsdialog.cpp
		ui/updateavailabledialog.cpp
		ui/updatecheckdialog.cpp
		scimpositionengine.cpp
		ui/impositiondialog.cpp
		ui/impositionpreviewwidget.cpp
		ui/tile_preview_widget.cpp
		offset_separation_presets.cpp
		offset_tiling.cpp
		ui/pdfseparationsviewer.cpp
		util_pdfsep.cpp
		${SCRIBUS_API_SOURCES}
		${SCRIBUS_COLORMGMT_SOURCES}
		${SCRIBUS_DESAXE_SOURCES}
		${SCRIBUS_FONTS_SOURCES}
		${SCRIBUS_STYLES_SOURCES}
		${SCRIBUS_TEXT_SOURCES}
		${SCRIBUS_WIN32_ONLY_SOURCES}
		${GESTURE_FRAME_PREVIEW_SOURCES}
	)
endif ()

if(WANT_PCH)
	target_precompile_headers(${EXE_NAME} PRIVATE scribus_pch.h)
endif()

if(WIN32)
	set_target_properties(${EXE_NAME}
		PROPERTIES
		COMPILE_FLAGS -DCOMPILE_SCRIBUS_MAIN_APP
		ENABLE_EXPORTS TRUE
	)
	set(WIN32_ONLY_LIBS mscms.lib)
else()
	set(WIN32_ONLY_LIBS)
endif()

##


#Set up libraries to link with all found packages
include_directories(${cairo_INCLUDE_DIR})
target_link_libraries(${EXE_NAME} PRIVATE ${cairo_LIBRARIES})
link_directories(${PREFIX_cairo_LIB})
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH};${PREFIX_cairo_LIB}")

message("RPATH: ${CMAKE_INSTALL_RPATH}")

target_link_libraries(${EXE_NAME}
	PUBLIC
	Qt6::Core
	Qt6::Concurrent
	Qt6::Core5Compat
	Qt6::Gui
	Qt6::Widgets
	Qt6::Network
	Qt6::Xml
	Qt6::PrintSupport
	Qt6::Svg
	)

if (Qt6Keychain_FOUND)
	target_link_libraries(${EXE_NAME} PRIVATE Qt6Keychain::Qt6Keychain)
endif()

# Updater: the installer runs as root, so a manifest is only trusted when it
# carries an Ed25519 signature from the release key. Without OpenSSL or
# without SCRIBUS_UPDATE_PUBKEY every update is refused (fail closed).
set(SCRIBUS_UPDATE_PUBKEY "" CACHE STRING "Base64 raw Ed25519 public key that signs update manifests")
find_package(OpenSSL QUIET COMPONENTS Crypto)
if (OpenSSL_FOUND OR OPENSSL_FOUND)
	target_link_libraries(${EXE_NAME} PRIVATE OpenSSL::Crypto)
	target_compile_definitions(${EXE_NAME} PRIVATE HAVE_UPDATE_SIGNING)
else()
	message(WARNING "OpenSSL not found: the updater will refuse every update")
endif()
if (SCRIBUS_UPDATE_PUBKEY)
	target_compile_definitions(${EXE_NAME} PRIVATE SCRIBUS_UPDATE_PUBKEY="${SCRIBUS_UPDATE_PUBKEY}")
else()
	message(STATUS "SCRIBUS_UPDATE_PUBKEY is empty: the updater will refuse every update")
endif()

# Updater: the version this build reports, "<upstream>-YYYYMMDD-N", set by
# tools/release.sh for every release so each one is strictly newer than the
# last. The same string is the .deb version. It goes into a generated header
# that only scupdateclient.cpp includes: file(CONFIGURE) rewrites it only when
# the value changes, so a new release recompiles that one file and relinks.
# (A per-source COMPILE_DEFINITIONS was tried first; the Makefile generator
# does not rebuild the object when only that define changes.)
# Empty (developer builds): the updater reports the plain upstream VERSION.
set(SCRIBUS_RELEASE_VERSION "" CACHE STRING "Release version for the updater and the .deb, e.g. 1.7.3-20260930-1")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/scupdate_release_version.h"
	CONTENT "// Generated by scribus/CMakeLists.txt from SCRIBUS_RELEASE_VERSION.\n#define SCRIBUS_RELEASE_VERSION \"${SCRIBUS_RELEASE_VERSION}\"\n"
	@ONLY)
target_include_directories(${EXE_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if (SCRIBUS_RELEASE_VERSION)
	message(STATUS "Updater release version: ${SCRIBUS_RELEASE_VERSION}")
endif()

# Updater: the server URL is NOT compiled in. It is read at run time from
# /etc/scribus/update.conf, which this installs (and the .deb ships as a
# conffile, so a locally edited line survives upgrades). The default written
# into that file comes from SCRIBUS_UPDATE_DEFAULT_URL - site-specific, so it
# stays out of the public repository and in the build cache of the release
# machine. Empty: the file is installed with no URL and the updater stays off
# until a URL is entered in Update Settings.
set(SCRIBUS_UPDATE_DEFAULT_URL "" CACHE STRING "Default update server URL written to /etc/scribus/update.conf, e.g. http://host.local:8081")
configure_file("${CMAKE_SOURCE_DIR}/resources/updater/update.conf.in" "${CMAKE_BINARY_DIR}/update.conf" @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/update.conf" DESTINATION /etc/scribus)

# Push installs from the update server (tools/push-update.sh). The helper is the
# only thing the push account may run as root: it installs a Scribus .deb only
# after verifying its signed manifest with the release public key above.
# SCRIBUS_PUSH_USER is that account on the office PCs; empty (the default)
# installs the helper but no sudoers entry, so nobody can use it without a
# password.
configure_file("${CMAKE_SOURCE_DIR}/resources/updater/scribus-install-update.in" "${CMAKE_BINARY_DIR}/scribus-install-update" @ONLY)
install(PROGRAMS "${CMAKE_BINARY_DIR}/scribus-install-update" DESTINATION /usr/local/sbin)
set(SCRIBUS_PUSH_USER "" CACHE STRING "Account on the office PCs that may run scribus-install-update with sudo, without a password")
if (SCRIBUS_PUSH_USER)
	if (NOT SCRIBUS_PUSH_USER MATCHES "^[a-z_][a-z0-9_-]*$")
		message(FATAL_ERROR "SCRIBUS_PUSH_USER must be a plain account name, not '${SCRIBUS_PUSH_USER}'")
	endif()
	configure_file("${CMAKE_SOURCE_DIR}/resources/updater/scribus-push.sudoers.in" "${CMAKE_BINARY_DIR}/scribus-push.sudoers" @ONLY)
	# sudo ignores a sudoers.d file that is not 0440, and names with a dot.
	install(FILES "${CMAKE_BINARY_DIR}/scribus-push.sudoers" DESTINATION /etc/sudoers.d RENAME scribus-push
	        PERMISSIONS OWNER_READ GROUP_READ)
	message(STATUS "Push installs: ${SCRIBUS_PUSH_USER} may run scribus-install-update without a password")
endif()

if (HAVE_OSG)
	target_link_libraries(${EXE_NAME}
		PUBLIC
		Qt6::OpenGL
		Qt6::OpenGLWidgets
		)
endif()

target_link_libraries(${EXE_NAME}
	PUBLIC
	${LIBXML2_LIBRARIES}
	${ZLIB_LIBRARIES}
	PRIVATE
	${CUPS_LIBRARIES}
	${FREETYPE_LIBRARIES}
	${GMAGICK_LIBRARIES}
	${HARFBUZZ_LIBRARIES}
	${HARFBUZZ_SUBSET_LIBRARIES}
	${hunspell_LIBRARIES}
	${ICU_LIBRARIES}
	${JPEG_LIBRARIES}
	${LCMS2_LIBRARIES}
	${PNG_LIBRARIES}
	${TIFF_LIBRARIES}
	${WIN32_ONLY_LIBS}
)

if (TARGET Fontconfig::Fontconfig)
	target_link_libraries(${EXE_NAME} PRIVATE Fontconfig::Fontconfig)
endif()

if (NOT WIN32 AND NOT HAIKU)
	target_link_libraries(${EXE_NAME} PRIVATE ${CUPS_LIBRARIES})
endif ()

# required to resolve symbols from libdl when ld is called with --no-add-needed
# #9310 for the OpenBSD addition
if(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" AND NOT CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" AND NOT HAIKU)
  target_link_libraries(${EXE_NAME} PUBLIC ${CMAKE_DL_LIBS})
endif()

# #9311 for the OpenBSD addition
if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
  target_link_libraries(${EXE_NAME} PRIVATE util execinfo)
endif()

if(HAIKU)
  target_link_libraries(${EXE_NAME} PRIVATE execinfo)
endif(HAIKU)

if(WIN32)
  target_link_libraries(${EXE_NAME} PRIVATE ${QT_QTMAIN_LIBRARY})
endif()

if(APPLE)
	target_link_libraries(${EXE_NAME}
		PUBLIC
		"-undefined dynamic_lookup"
		PRIVATE
		"-framework CoreFoundation"
		)
	target_link_options(${EXE_NAME}
		PUBLIC
		"-Wl,-no_fixup_chains"
	)
endif()

if (WANT_SVNVERSION)
	add_dependencies(${EXE_NAME} svnheader)
endif ()


target_link_libraries(${EXE_NAME} PRIVATE
	${SCRIBUS_WPG_LIB}
	${SCRIBUS_PGF_LIB}
	${SCRIBUS_ZIP_LIB}
	${SCRIBUS_RTF_LIB}
)

#if(WANT_QTADS)
  target_link_libraries(${EXE_NAME} PUBLIC qt6advanceddocking)
#endif()

if(WITH_TESTS)
	target_link_libraries(${EXE_NAME} PRIVATE
		${QT_QTTEST_LIBRARY}
		${SCRIBUS_TESTS_LIB}
	)
endif()

if(HAVE_PODOFO)
	target_link_libraries(${EXE_NAME} PRIVATE ${LIBPODOFO_LIBRARY})
endif()

if(HAVE_JXL)
	target_link_libraries(${EXE_NAME} PRIVATE ${JXL_LIBRARY} ${JXL_CMS_LIBRARY} ${JXL_THREADS_LIBRARY})
endif()

if(HAVE_OSG)
	target_link_libraries(${EXE_NAME} PRIVATE
		${OPENTHREADS_LIBRARY}
		${OSG_LIBRARY}
		${OSGGA_LIBRARY}
		${OSGUTIL_LIBRARY}
		${OSGDB_LIBRARY}
		${OSGVIEWER_LIBRARY}
		${OSGTEXT_LIBRARY}
		${SCRIBUS_PRC_LIB}
	)
endif()

if (HAVE_BOOST)
	target_link_libraries(${EXE_NAME} PRIVATE ${SCRIBUS_2GEOM_LIB})
endif()

# SAM Smart Select (Phase 4C) — optional ONNX Runtime inference.
option(WITH_SAM "Build the SAM Smart Select tool (requires ONNX Runtime)" ON)
if (WITH_SAM)
	find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h PATH_SUFFIXES onnxruntime)
	find_library(ONNXRUNTIME_LIBRARY onnxruntime)
	if (ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
		message(STATUS "SAM Smart Select: ONNX Runtime found (${ONNXRUNTIME_LIBRARY})")
		target_include_directories(${EXE_NAME} PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
		target_link_libraries(${EXE_NAME} PRIVATE ${ONNXRUNTIME_LIBRARY})
		target_compile_definitions(${EXE_NAME} PRIVATE HAVE_SAM)
	else()
		message(STATUS "SAM Smart Select: ONNX Runtime NOT found — building the tool as a stub")
	endif()
endif()

# Now build plugins

# IF your plugin needs additional libraries, please link them privately
# in the CMakeLists.txt for your plugin.

add_custom_target(Plugins ALL)

target_sources(Plugins
  PRIVATE
    designer/uiwidgets.h designer/uiwidgets.cpp
    designer/formwidget_plugin.h designer/formwidget_plugin.cpp
    designer/sectioncontainer_extension.h designer/sectioncontainer_extension.cpp
    designer/sectioncontainer_extensionfactory.h designer/sectioncontainer_extensionfactory.cpp
    designer/sectioncontainer_plugin.h designer/sectioncontainer_plugin.cpp
)
add_subdirectory(plugins)

#do we want a lib suffix on osx? doubt it
if(APPLEBUNDLE)
	install(DIRECTORY DESTINATION MacOS)
	install(TARGETS ${EXE_NAME}
	RUNTIME DESTINATION MacOS
	LIBRARY DESTINATION lib
	)
#	add_custom_command(TARGET ${EXE_NAME} POST_BUILD
#	COMMAND ${CMAKE_COMMAND} ARGS -Dinput_file=${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}.app/Contents/MacOS/${EXE_NAME} -P ${CMAKE_MODULE_PATH}/ScribusIngestOSXBundleLibraries.cmake
#	)
#message(STATUS "Installing apple bundle")

#	set(APPS "${APPLE_APP_DIR}")
#	set(DIRS ${QT_LIBRARY_DIRS})
#	install(CODE "
#		file(GLOB_RECURSE QTPLUGINS
#		  \"\${CMAKE_INSTALL_PREFIX}/${LIBDIR}/plugins/*${CMAKE_SHARED_LIBRARY_SUFFIX}\")
#		include(BundleUtilities)
#		fixup_bundle(\"${APPS}\" \"\${QTPLUGINS}\" \"${DIRS}\")
#		" COMPONENT Runtime)

else()
	if(WIN32)
		SET(RUNTIME_DEST ".")
	else()
		SET(RUNTIME_DEST "bin")
	endif()
	install(TARGETS ${EXE_NAME}
		RUNTIME DESTINATION ${RUNTIME_DEST}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	)
endif()

#Install our header files, selected from all existing dirs, excluding unwanted directories and svn files
if(WANT_HEADERINSTALL)
	message(STATUS "Source header files will be installed")
	install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION ${INCLUDEDIR}
		FILES_MATCHING
		PATTERN "*.h"
		PATTERN "dicts" EXCLUDE
		PATTERN "doc" EXCLUDE
		PATTERN "dtd" EXCLUDE
		PATTERN "editorconfig" EXCLUDE
		PATTERN "icon*" EXCLUDE
		PATTERN "manpages" EXCLUDE
		PATTERN "old" EXCLUDE
		PATTERN ".svn" EXCLUDE
	)
else()
	message(STATUS "No source header files will be installed")
endif()

#Install our stylesheet
install(FILES
	scribus.css
	DESTINATION ${LIBDIR}
)
