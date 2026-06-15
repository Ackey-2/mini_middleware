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

    add_executable(test_subscriber test_subscriber.cpp)
    target_link_libraries(test_subscriber PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_subscriber COMMAND test_subscriber)

    add_executable(test_publisher test_publisher.cpp)
    target_link_libraries(test_publisher PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_publisher COMMAND test_publisher)

    add_executable(test_node test_node.cpp)
    target_link_libraries(test_node PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_node COMMAND test_node)

    add_executable(test_discovery_proto test_discovery_proto.cpp)
    target_link_libraries(test_discovery_proto PRIVATE
        mm_proto
        GTest::gtest_main
    )
    add_test(NAME test_discovery_proto COMMAND test_discovery_proto)

    add_executable(test_udp_multicast test_udp_multicast.cpp)
    target_link_libraries(test_udp_multicast PRIVATE
        mm_discovery
        GTest::gtest_main
    )
    add_test(NAME test_udp_multicast COMMAND test_udp_multicast)

    add_executable(test_endpoint_matcher test_endpoint_matcher.cpp)
    target_link_libraries(test_endpoint_matcher PRIVATE
        mm_discovery
        GTest::gtest_main
    )
    add_test(NAME test_endpoint_matcher COMMAND test_endpoint_matcher)
else()
    message(WARNING "gtest not found, skipping tests")
endif()
