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

