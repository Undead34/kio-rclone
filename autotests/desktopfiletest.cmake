if(NOT IS_ABSOLUTE "${EXPECTED_EXECUTABLE}")
    message(FATAL_ERROR "Expected launcher executable is not absolute: ${EXPECTED_EXECUTABLE}")
endif()

file(STRINGS "${DESKTOP_FILE}" execLines REGEX "^Exec=")
list(LENGTH execLines execLineCount)
if(NOT execLineCount EQUAL 1)
    message(FATAL_ERROR "Expected exactly one Exec line in ${DESKTOP_FILE}, found ${execLineCount}")
endif()

set(expectedExecLine "Exec=\"${EXPECTED_EXECUTABLE}\"")
if(NOT execLines STREQUAL expectedExecLine)
    message(FATAL_ERROR "Unexpected launcher command: ${execLines}; expected ${expectedExecLine}")
endif()
