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
