add_executable(kio-rclone-config
    configwindow.cpp
    main.cpp
)

target_link_libraries(kio-rclone-config
    PRIVATE
        kio_rclone_common
        Qt6::Widgets
        KF6::DBusAddons
        KF6::I18n
        KF6::WindowSystem
)

install(TARGETS kio-rclone-config
    ${KDE_INSTALL_TARGETS_DEFAULT_ARGS}
)

install(PROGRAMS org.kde.kio-rclone-config.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install(FILES org.kde.kio-rclone-config.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)

install(FILES ${CMAKE_SOURCE_DIR}/docs/public/kio-rclone.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
    RENAME org.kde.kio-rclone-config.svg
)

install(FILES folder-rclone-outline.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/places
    RENAME folder-kio-rclone.svg
)
