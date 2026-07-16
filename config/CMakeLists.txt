add_executable(kio-rclone-config
    configwindow.cpp
    main.cpp
)

target_link_libraries(kio-rclone-config
    PRIVATE
        kio_rclone_common
        Qt6::Widgets
        KF6::I18n
)

install(TARGETS kio-rclone-config
    ${KDE_INSTALL_TARGETS_DEFAULT_ARGS}
)

install(PROGRAMS org.kde.kio-rclone-config.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)
