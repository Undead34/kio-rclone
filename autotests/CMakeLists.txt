add_executable(rcloneurltest
    rcloneurltest.cpp
)
target_link_libraries(rcloneurltest
    PRIVATE
        kio_rclone_common
        Qt6::Test
)
add_test(NAME rcloneurltest COMMAND rcloneurltest)

add_executable(rcloneclienttest
    rcloneclienttest.cpp
)
target_link_libraries(rcloneclienttest
    PRIVATE
        kio_rclone_common
        Qt6::Test
)
add_test(NAME rcloneclienttest COMMAND rcloneclienttest)

add_executable(directorysnapshotcacheprobe
    directorysnapshotcacheprobe.cpp
)
target_link_libraries(directorysnapshotcacheprobe
    PRIVATE
        kio_rclone_common
        Qt6::Core
)

add_executable(directorysnapshotcachetest
    directorysnapshotcachetest.cpp
)
target_compile_definitions(directorysnapshotcachetest
    PRIVATE
        DIRECTORY_SNAPSHOT_CACHE_PROBE="$<TARGET_FILE:directorysnapshotcacheprobe>"
)
target_link_libraries(directorysnapshotcachetest
    PRIVATE
        kio_rclone_common
        Qt6::Test
)
add_dependencies(directorysnapshotcachetest
    directorysnapshotcacheprobe
)
add_test(NAME directorysnapshotcachetest COMMAND directorysnapshotcachetest)

add_executable(fake-rclone
    fake-rclone.cpp
)
target_link_libraries(fake-rclone
    PRIVATE
        Qt6::Core
)

target_compile_definitions(rcloneclienttest
    PRIVATE
        FAKE_RCLONE_PATH="$<TARGET_FILE:fake-rclone>"
)
add_dependencies(rcloneclienttest
    fake-rclone
)

add_executable(rclonepausetest
    rclonepausetest.cpp
)
target_compile_definitions(rclonepausetest
    PRIVATE
        FAKE_RCLONE_PATH="$<TARGET_FILE:fake-rclone>"
)
target_link_libraries(rclonepausetest
    PRIVATE
        Qt6::Test
        KF6::KIOCore
)
add_dependencies(rclonepausetest
    fake-rclone
    kio_rclone
)
add_test(NAME rclonepausetest COMMAND rclonepausetest)
set_tests_properties(rclonepausetest PROPERTIES
    TIMEOUT 30
)

add_executable(rclonedownloadtest
    rclonedownloadtest.cpp
)
target_compile_definitions(rclonedownloadtest
    PRIVATE
        FAKE_RCLONE_PATH="$<TARGET_FILE:fake-rclone>"
)
target_link_libraries(rclonedownloadtest
    PRIVATE
        Qt6::Test
        KF6::KIOCore
)
add_dependencies(rclonedownloadtest
    fake-rclone
    kio_rclone
)
add_test(NAME rclonedownloadtest COMMAND rclonedownloadtest)
set_tests_properties(rclonedownloadtest PROPERTIES
    TIMEOUT 30
)

add_executable(rclonedirectorycachetest
    rclonedirectorycachetest.cpp
)
target_compile_definitions(rclonedirectorycachetest
    PRIVATE
        FAKE_RCLONE_PATH="$<TARGET_FILE:fake-rclone>"
)
target_link_libraries(rclonedirectorycachetest
    PRIVATE
        kio_rclone_common
        Qt6::Test
        KF6::KIOCore
)
add_dependencies(rclonedirectorycachetest
    fake-rclone
    kio_rclone
)
add_test(NAME rclonedirectorycachetest COMMAND rclonedirectorycachetest)
set_tests_properties(rclonedirectorycachetest PROPERTIES
    TIMEOUT 30
)

add_executable(rcloneuploadtest
    rcloneuploadtest.cpp
)
target_compile_definitions(rcloneuploadtest
    PRIVATE
        FAKE_RCLONE_PATH="$<TARGET_FILE:fake-rclone>"
)
target_link_libraries(rcloneuploadtest
    PRIVATE
        Qt6::Test
        KF6::KIOCore
)
add_dependencies(rcloneuploadtest
    fake-rclone
    kio_rclone
)
add_test(NAME rcloneuploadtest COMMAND rcloneuploadtest)
set_tests_properties(rcloneuploadtest PROPERTIES
    TIMEOUT 45
)

add_executable(rclonepromptdialogtest
    rclonepromptdialogtest.cpp
    ${CMAKE_SOURCE_DIR}/app/dialogs/rclonepromptdialog.cpp
)
target_include_directories(rclonepromptdialogtest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app
)
target_link_libraries(rclonepromptdialogtest
    PRIVATE
        kio_rclone_common
        Qt6::Test
        Qt6::Widgets
        KF6::I18n
)
add_test(NAME rclonepromptdialogtest COMMAND rclonepromptdialogtest)
set_tests_properties(rclonepromptdialogtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_test(
    NAME desktopfiletest
    COMMAND ${CMAKE_COMMAND}
        "-DDESKTOP_FILE=${CMAKE_BINARY_DIR}/app/org.kde.kio-rclone-config.desktop"
        "-DEXPECTED_EXECUTABLE=${KDE_INSTALL_FULL_BINDIR}/kio-rclone-config"
        "-DEXPECTED_LAUNCH_SCHEME=${KIO_RCLONE_CONFIG_LAUNCH_SCHEME}"
        "-DEXPECTED_LAUNCH_MIME_TYPE=${KIO_RCLONE_CONFIG_LAUNCH_MIME_TYPE}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/desktopfiletest.cmake
)

add_test(
    NAME mimefiletest
    COMMAND ${CMAKE_COMMAND}
        "-DMIME_FILE=${CMAKE_BINARY_DIR}/app/org.kde.kio-rclone-config.xml"
        "-DEXPECTED_LAUNCH_MIME_TYPE=${KIO_RCLONE_CONFIG_LAUNCH_MIME_TYPE}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/mimefiletest.cmake
)
