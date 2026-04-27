# 测试用 gtest
find_package(GTest QUIET)

if(GTest_FOUND)
    add_executable(test_frame_codec test_frame_codec.cpp)
    target_link_libraries(test_frame_codec PRIVATE
        mm_transport
        GTest::gtest_main
    )
    add_test(NAME test_frame_codec COMMAND test_frame_codec)
else()
    message(WARNING "gtest not found, skipping tests")
endif()