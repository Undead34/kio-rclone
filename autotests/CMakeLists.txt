add_executable(rcloneurltest
    rcloneurltest.cpp
)
target_link_libraries(rcloneurltest
    PRIVATE
        kio_rclone_common
        Qt6::Test
)
add_test(NAME rcloneurltest COMMAND rcloneurltest)

add_executable(rclonebackendtest
    rclonebackendtest.cpp
)
target_link_libraries(rclonebackendtest
    PRIVATE
        kio_rclone_common
        Qt6::Test
)
add_test(NAME rclonebackendtest COMMAND rclonebackendtest)

add_executable(fake-rclone
    fake-rclone.cpp
)
target_link_libraries(fake-rclone
    PRIVATE
        Qt6::Core
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
