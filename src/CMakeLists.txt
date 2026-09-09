add_library(kio_rclone_common STATIC
  rclone/client.cpp
  rclone/location.cpp
  rclone/models.cpp
  rclone/navigation.cpp
  rclone/navigationentry.cpp
  rclone/url.cpp
)

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
        kio/worker.cpp
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
