add_library(kio_rclone_drive_action_logic STATIC
    driveactiontarget.cpp
    drivemetadataresolver.cpp
    googleurls.cpp
)

target_include_directories(kio_rclone_drive_action_logic
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(kio_rclone_drive_action_logic
    PUBLIC
        kio_rclone_common
)

add_executable(kio-rclone-drive-action
    main.cpp
)

target_link_libraries(kio-rclone-drive-action
    PRIVATE
        kio_rclone_drive_action_logic
        Qt6::DBus
        Qt6::Widgets
        KF6::I18n
)

kcoreaddons_add_plugin(kio_rclone_google_drive_action
    SOURCES
        googledriveactionplugin.cpp
    INSTALL_NAMESPACE kf6/kfileitemaction
)

target_link_libraries(kio_rclone_google_drive_action
    PRIVATE
        kio_rclone_drive_action_logic
        Qt6::Core
        Qt6::Widgets
        KF6::CoreAddons
        KF6::I18n
        KF6::KIOWidgets
)

target_compile_definitions(kio_rclone_google_drive_action
    PRIVATE
        KIO_RCLONE_DRIVE_ACTION_EXECUTABLE="${KDE_INSTALL_FULL_BINDIR}/kio-rclone-drive-action"
)

install(TARGETS kio-rclone-drive-action
    ${KDE_INSTALL_TARGETS_DEFAULT_ARGS}
)

if(BUILD_TESTING)
    add_test(
        NAME dolphin-actions-plugin-metadata
        COMMAND ${CMAKE_COMMAND}
            -DPLUGIN_JSON=${CMAKE_CURRENT_SOURCE_DIR}/googledriveactionplugin.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pluginmetadatatest.cmake
    )
endif()
