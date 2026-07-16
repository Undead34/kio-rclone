add_library(kio_rclone_common STATIC
    rclonebackend.cpp
    rcloneurl.cpp
)

set_target_properties(kio_rclone_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(kio_rclone_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kio_rclone_common
    PUBLIC
        Qt6::Core
)

kcoreaddons_add_plugin(kio_rclone
    SOURCES
        rcloneworker.cpp
    INSTALL_NAMESPACE kf6/kio
)

target_link_libraries(kio_rclone
    PRIVATE
        kio_rclone_common
        Qt6::Core
        KF6::I18n
        KF6::KIOCore
)

set_target_properties(kio_rclone PROPERTIES
    OUTPUT_NAME rclone
)
