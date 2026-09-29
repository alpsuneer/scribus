include_directories(
${CMAKE_SOURCE_DIR}
${CMAKE_SOURCE_DIR}/scribus
${FREETYPE_INCLUDE_DIRS}
${SCRIBUS_AUTOGEN_INCLUDE_PATH}
..
)

set(SCRIBUS_TEST_SOURCES
runtests.cpp
#testIndex.cpp
testStoryText.cpp
)

set(SCRIBUS_TESTS_LIB "scribus_tests_lib")
add_library(${SCRIBUS_TESTS_LIB} STATIC ${SCRIBUS_TEST_SOURCES})


# This is a convenience library that for linkage purposes is part of Scribus's
# main API.
set_target_properties(${SCRIBUS_TESTS_LIB}
  PROPERTIES
  COMPILE_FLAGS -DCOMPILE_SCRIBUS_MAIN_APP
  )


# Regular unit tests below.
#
# These tests are built as standalone executables and are run using "make test"
#

# QT_QTTEST_LIBRARY / QT_LIBRARIES are Qt4-era variables and are empty in this
# Qt6 build, so every target that linked through them failed at link time. Use
# the imported Qt6 targets instead.
find_package(Qt6 COMPONENTS Test REQUIRED)
set(TESTS_LIBRARIES Qt6::Test Qt6::Gui Qt6::Core)

# Unit tests for CellArea
set(CELLAREATESTS_SOURCES cellareatests.cpp ../cellarea.cpp)
add_executable(cellareatests ${CELLAREATESTS_SOURCES})
target_link_libraries(cellareatests ${TESTS_LIBRARIES})
add_test(NAME cellareatests COMMAND cellareatests)

# Unit tests for the non-destructive image eraser mask
set(ERASERMASKTESTS_SOURCES erasermasktests.cpp ../scimageerasermask.cpp)
add_executable(erasermasktests ${ERASERMASKTESTS_SOURCES})
target_link_libraries(erasermasktests ${TESTS_LIBRARIES})
add_test(NAME erasermasktests COMMAND erasermasktests)

# Unit tests for eraser-mask contour tracing
set(CONTOURDETECTTESTS_SOURCES contourdetecttests.cpp ../util_contour.cpp)
add_executable(contourdetecttests ${CONTOURDETECTTESTS_SOURCES})
target_link_libraries(contourdetecttests ${TESTS_LIBRARIES})
add_test(NAME contourdetecttests COMMAND contourdetecttests)


# Unit tests for the frame-shape geometry in util_math, plus the interactive
# shape-crop overlay's geometry in util_shapecrop (coordinate transforms,
# handle hit-testing, resize/rotate maths). Pure maths: no GUI, no document,
# and no Qt Widgets - which is why it covers the shapes this fork generates
# and not the four it reuses from Scribus's own tables, and why the overlay
# geometry lives in its own util_shapecrop.cpp rather than inside
# CanvasMode_ShapeCrop, which pulls in Canvas/ScribusView/ScribusDoc.
set(FRAMESHAPETESTS_SOURCES frameshapetests.cpp ../util_math.cpp ../fpointarray.cpp ../fpoint.cpp ../util_shapecrop.cpp)
add_executable(frameshapetests ${FRAMESHAPETESTS_SOURCES})
target_link_libraries(frameshapetests ${TESTS_LIBRARIES})
add_test(NAME frameshapetests COMMAND frameshapetests)


# Unit tests for Telea fast-marching inpainting
set(INPAINTTESTS_SOURCES inpainttests.cpp ../util_inpaint.cpp)
add_executable(inpainttests ${INPAINTTESTS_SOURCES})
target_link_libraries(inpainttests ${TESTS_LIBRARIES})
add_test(NAME inpainttests COMMAND inpainttests)

# Unit tests for the IOPaint/LaMa inpainting client.
# Runs against a mock server in-process; never touches a real IOPaint.
find_package(Qt6 COMPONENTS Network REQUIRED)
set(LAMAINPAINTSERVICETESTS_SOURCES
	lamainpaintservicetests.cpp
	../ai/aiinpaintservice.cpp
	../ai/lamainpaintservice.cpp
)
add_executable(lamainpaintservicetests ${LAMAINPAINTSERVICETESTS_SOURCES})
target_link_libraries(lamainpaintservicetests ${TESTS_LIBRARIES} Qt6::Network)
add_test(NAME lamainpaintservicetests COMMAND lamainpaintservicetests)

# Unit tests for the OpenRouter inpainting client.
# Runs against a mock server in-process. Nothing here contacts openrouter.ai:
# these must pass on a machine with no account, no key and no network, and a
# suite that spends money per run is not one anybody keeps running.
set(OPENROUTERINPAINTSERVICETESTS_SOURCES
	openrouterinpaintservicetests.cpp
	../ai/aiinpaintservice.cpp
	../ai/openrouterinpaintservice.cpp
)
add_executable(openrouterinpaintservicetests ${OPENROUTERINPAINTSERVICETESTS_SOURCES})
target_link_libraries(openrouterinpaintservicetests ${TESTS_LIBRARIES} Qt6::Network)
add_test(NAME openrouterinpaintservicetests COMMAND openrouterinpaintservicetests)

# Unit tests for the Google Gemini inpainting client.
# Runs against a mock server in-process. Nothing here contacts
# generativelanguage.googleapis.com, for the same reasons as above.
set(GEMINIINPAINTSERVICETESTS_SOURCES
	geminiinpaintservicetests.cpp
	../ai/aiinpaintservice.cpp
	../ai/geminiinpaintservice.cpp
)
add_executable(geminiinpaintservicetests ${GEMINIINPAINTSERVICETESTS_SOURCES})
target_link_libraries(geminiinpaintservicetests ${TESTS_LIBRARIES} Qt6::Network)
add_test(NAME geminiinpaintservicetests COMMAND geminiinpaintservicetests)

# Unit tests for the three AI *text* clients. Each runs against the shared
# in-process mock in aitextmockserver.h and contacts nothing: these must pass on
# a machine with no account, no key and no network, and this fork's user has
# billing for one provider only.
set(AITEXT_SHARED_SOURCES
	../ai/aitextservice.cpp
	../ai/aitextprompts.cpp
	../ai/aitexthttpservice.cpp
)

add_executable(claudetextservicetests
	claudetextservicetests.cpp ${AITEXT_SHARED_SOURCES} ../ai/claudetextservice.cpp)
target_link_libraries(claudetextservicetests ${TESTS_LIBRARIES} Qt6::Network)
add_test(NAME claudetextservicetests COMMAND claudetextservicetests)

add_executable(geminitextservicetests
	geminitextservicetests.cpp ${AITEXT_SHARED_SOURCES} ../ai/geminitextservice.cpp)
target_link_libraries(geminitextservicetests ${TESTS_LIBRARIES} Qt6::Network)
add_test(NAME geminitextservicetests COMMAND geminitextservicetests)

add_executable(openaitextservicetests
	openaitextservicetests.cpp ${AITEXT_SHARED_SOURCES} ../ai/openaitextservice.cpp)
target_link_libraries(openaitextservicetests ${TESTS_LIBRARIES} Qt6::Network)
add_test(NAME openaitextservicetests COMMAND openaitextservicetests)

# Unit tests for the Offset Separations print-dialog tab: bundled preset
# values, custom preset JSON round-trip, and the PostScript halftone/spot
# function text generation. Pure data/string logic - no GUI, no ScribusDoc,
# no PostScript interpreter involved, so this never touches pslib.cpp itself.
set(OFFSETSEPARATIONTESTS_SOURCES offsetseparationtests.cpp ../offset_separation_presets.cpp)
add_executable(offsetseparationtests ${OFFSETSEPARATIONTESTS_SOURCES})
target_link_libraries(offsetseparationtests ${TESTS_LIBRARIES})
add_test(NAME offsetseparationtests COMMAND offsetseparationtests)

# Unit tests for the Offset Separations tab's auto-tiling math (grid
# calculation, overlap, labelling) and per-tile PostScript mark/label text
# generation. Pure data/string logic, same rationale as offsetseparationtests
# above - never touches pslib.cpp or a live printer.
set(OFFSETTILINGTESTS_SOURCES offsettilingtests.cpp ../offset_tiling.cpp)
add_executable(offsettilingtests ${OFFSETTILINGTESTS_SOURCES})
target_link_libraries(offsettilingtests ${TESTS_LIBRARIES})
add_test(NAME offsettilingtests COMMAND offsettilingtests)
