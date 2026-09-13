add_library(kio_rclone_common STATIC
  rclone/client.cpp
  rclone/clienthelpers.cpp
  rclone/location.cpp
  rclone/models.cpp
  rclone/mountlayout.cpp
  rclone/navigation.cpp
  rclone/navigationentry.cpp
  rclone/remotefile.cpp
  rclone/syncrepository.cpp
  rclone/url.cpp
)

# KIO-FUSE 5.x selects its per-operation FileJob path only when both
# capabilities are advertised.  Random-access FileJob methods are not
# implemented by this worker, so keep them disabled and let KIO-FUSE use its
# local cache (KIO::get/KIO::put) for rclone:/.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/kio/rclone.json" RCLONE_PROTOCOL_METADATA)
string(JSON RCLONE_CLASS GET "${RCLONE_PROTOCOL_METADATA}"
       KDE-KIO-Protocols rclone Class)
string(JSON RCLONE_OPENING_TYPE TYPE "${RCLONE_PROTOCOL_METADATA}"
       KDE-KIO-Protocols rclone opening)
string(JSON RCLONE_TRUNCATING_TYPE TYPE "${RCLONE_PROTOCOL_METADATA}"
       KDE-KIO-Protocols rclone truncating)
string(JSON RCLONE_OPENING GET "${RCLONE_PROTOCOL_METADATA}"
       KDE-KIO-Protocols rclone opening)
string(JSON RCLONE_TRUNCATING GET "${RCLONE_PROTOCOL_METADATA}"
       KDE-KIO-Protocols rclone truncating)

if(NOT RCLONE_CLASS STREQUAL ":local"
   OR NOT RCLONE_OPENING_TYPE STREQUAL "BOOLEAN"
   OR NOT RCLONE_TRUNCATING_TYPE STREQUAL "BOOLEAN"
   OR RCLONE_OPENING
   OR RCLONE_TRUNCATING)
    message(FATAL_ERROR
        "The rclone KIO protocol must be :local and explicitly disable opening "
        "and truncating so KIO-FUSE uses its local cache")
endif()

set_target_properties(kio_rclone_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(kio_rclone_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(kio_rclone_common
    PRIVATE
        KIO_RCLONE_CONFIG_LAUNCH_SCHEME="${KIO_RCLONE_CONFIG_LAUNCH_SCHEME}"
        KIO_RCLONE_CONFIG_LAUNCH_MIME_TYPE="${KIO_RCLONE_CONFIG_LAUNCH_MIME_TYPE}"
)

target_link_libraries(kio_rclone_common
    PUBLIC
        Qt6::Core
        KF6::ConfigCore
        KF6::CoreAddons
        KF6::I18n
)

kcoreaddons_add_plugin(kio_rclone
    SOURCES
        kio/main.cpp
        kio/worker.cpp
        kio/workerhelpers.cpp
    INSTALL_NAMESPACE kf6/kio
)

target_link_libraries(kio_rclone
    PRIVATE
        kio_rclone_common
        Qt6::Core
        Qt6::DBus
        KF6::I18n
        KF6::KIOCore
)

set_target_properties(kio_rclone PROPERTIES
    OUTPUT_NAME rclone
)

add_subdirectory(dolphin-actions)
