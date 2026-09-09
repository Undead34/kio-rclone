if(NOT DEFINED PLUGIN_JSON OR NOT EXISTS "${PLUGIN_JSON}")
    message(FATAL_ERROR "PLUGIN_JSON must name the Google Drive action metadata file")
endif()

file(READ "${PLUGIN_JSON}" metadata)

foreach(required_fragment
        "\"Name\": \"KIO Rclone Google Drive actions\""
        "\"KFileItemAction/Plugin\""
        "\"application/octet-stream\""
        "\"inode/directory\"")
    string(FIND "${metadata}" "${required_fragment}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "Missing plugin metadata: ${required_fragment}")
    endif()
endforeach()
