# Office PDF export presets, shipped read-only (see scribus/pdfpresets.h).
# tools/update-office-pdf-presets.py refreshes this folder from the release
# laptop's presets that are marked "office"; office-default.txt, when present,
# names the one that is the office Default.
# install(DIRECTORY) reads the folder at install time, so a preset added after
# the build tree was configured is still packaged.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
DESTINATION ${SHAREDIR}pdf-presets
FILES_MATCHING
PATTERN "*.json"
PATTERN "office-default.txt"
PATTERN "README.txt"
)
