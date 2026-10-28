install(FILES learn_models.py analyze.py slaparse.py page_models.py
 DESTINATION ${SHAREDIR}autolayout
)
# the house rules measured from our pages: "Reset to house default" of the Layout Model gaps
install(FILES ${CMAKE_SOURCE_DIR}/resources/autolayout/autolayout-rules.json
 DESTINATION ${SHAREDIR}autolayout
)
