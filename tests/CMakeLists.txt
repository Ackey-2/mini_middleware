# 测试用 gtest
find_package(GTest QUIET)

if(GTest_FOUND)
    add_executable(test_frame_codec test_frame_codec.cpp)
    target_link_libraries(test_frame_codec PRIVATE
        mm_transport
        GTest::gtest_main
    )
    add_test(NAME test_frame_codec COMMAND test_frame_codec)

    add_executable(test_local_bus test_local_bus.cpp)
    target_link_libraries(test_local_bus PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_local_bus COMMAND test_local_bus)
else()
    message(WARNING "gtest not found, skipping tests")
endif()