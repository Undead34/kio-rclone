add_executable(driveactiontargettest
    driveactiontargettest.cpp
)

target_link_libraries(driveactiontargettest
    PRIVATE
        kio_rclone_drive_action_logic
        Qt6::Test
)

add_test(NAME driveactiontargettest COMMAND driveactiontargettest)

add_executable(dolphinactionplugintest
    dolphinactionplugintest.cpp
)

target_link_libraries(dolphinactionplugintest
    PRIVATE
        kio_rclone_drive_action_logic
        Qt6::Test
        Qt6::Widgets
        KF6::CoreAddons
        KF6::KIOWidgets
)

target_compile_definitions(dolphinactionplugintest
    PRIVATE
        KIO_RCLONE_ACTION_PLUGIN_BINARY="$<TARGET_FILE:kio_rclone_google_drive_action>"
)

add_dependencies(dolphinactionplugintest kio_rclone_google_drive_action)

add_test(NAME dolphinactionplugintest COMMAND dolphinactionplugintest)
set_tests_properties(dolphinactionplugintest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
