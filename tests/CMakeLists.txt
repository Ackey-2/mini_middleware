# 测试用 gtest
find_package(GTest QUIET)

if(GTest_FOUND)
    add_executable(test_host_id test_host_id.cpp)
    target_link_libraries(test_host_id PRIVATE
        mm_common
        GTest::gtest_main
    )
    add_test(NAME test_host_id COMMAND test_host_id)

    add_executable(test_qos test_qos.cpp)
    target_link_libraries(test_qos PRIVATE
        mm_common
        GTest::gtest_main
    )
    add_test(NAME test_qos COMMAND test_qos)

    add_executable(test_blocking_queue_capacity test_blocking_queue_capacity.cpp)
    target_link_libraries(test_blocking_queue_capacity PRIVATE
        mm_common
        GTest::gtest_main
    )
    add_test(NAME test_blocking_queue_capacity COMMAND test_blocking_queue_capacity)

    add_executable(test_shm_ring test_shm_ring.cpp)
    target_link_libraries(test_shm_ring PRIVATE
        mm_transport
        GTest::gtest_main
    )
    add_test(NAME test_shm_ring COMMAND test_shm_ring)

    add_executable(test_shm_segment test_shm_segment.cpp)
    target_link_libraries(test_shm_segment PRIVATE
        mm_transport
        GTest::gtest_main
    )
    add_test(NAME test_shm_segment COMMAND test_shm_segment)

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

    add_executable(test_rpc_topics test_rpc_topics.cpp)
    target_link_libraries(test_rpc_topics PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_rpc_topics COMMAND test_rpc_topics)

    add_executable(test_rpc_local test_rpc_local.cpp)
    target_link_libraries(test_rpc_local PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_rpc_local COMMAND test_rpc_local)

    add_executable(test_rpc_timeout test_rpc_timeout.cpp)
    target_link_libraries(test_rpc_timeout PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_rpc_timeout COMMAND test_rpc_timeout)

    add_executable(test_rpc_pubsub test_rpc_pubsub.cpp)
    target_link_libraries(test_rpc_pubsub PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_rpc_pubsub COMMAND test_rpc_pubsub)

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

    add_executable(test_data_message test_data_message.cpp)
    target_link_libraries(test_data_message PRIVATE
        mm_proto
        GTest::gtest_main
    )
    add_test(NAME test_data_message COMMAND test_data_message)

    add_executable(test_rpc_proto test_rpc_proto.cpp)
    target_link_libraries(test_rpc_proto PRIVATE
        mm_proto
        GTest::gtest_main
    )
    add_test(NAME test_rpc_proto COMMAND test_rpc_proto)

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

    add_executable(test_qos_match test_qos_match.cpp)
    target_link_libraries(test_qos_match PRIVATE
        mm_discovery
        GTest::gtest_main
    )
    add_test(NAME test_qos_match COMMAND test_qos_match)

    add_executable(test_discovery_agent test_discovery_agent.cpp)
    target_link_libraries(test_discovery_agent PRIVATE
        mm_discovery
        GTest::gtest_main
    )
    add_test(NAME test_discovery_agent COMMAND test_discovery_agent)

    add_executable(test_node_discovery test_node_discovery.cpp)
    target_link_libraries(test_node_discovery PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_node_discovery COMMAND test_node_discovery)

    add_executable(test_tcp_ephemeral_port test_tcp_ephemeral_port.cpp)
    target_link_libraries(test_tcp_ephemeral_port PRIVATE
        mm_transport
        GTest::gtest_main
    )
    add_test(NAME test_tcp_ephemeral_port COMMAND test_tcp_ephemeral_port)

    add_executable(test_shm_sink_reader test_shm_sink_reader.cpp)
    target_link_libraries(test_shm_sink_reader PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_shm_sink_reader COMMAND test_shm_sink_reader)

    add_executable(test_remote_sink test_remote_sink.cpp)
    target_link_libraries(test_remote_sink PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_remote_sink COMMAND test_remote_sink)

    add_executable(test_data_plane test_data_plane.cpp)
    target_link_libraries(test_data_plane PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_data_plane COMMAND test_data_plane)

    add_executable(test_qos_routing test_qos_routing.cpp)
    target_link_libraries(test_qos_routing PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_qos_routing COMMAND test_qos_routing)

    add_executable(test_tcp_pubsub test_tcp_pubsub.cpp)
    target_link_libraries(test_tcp_pubsub PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_tcp_pubsub COMMAND test_tcp_pubsub)

    add_executable(test_shm_pubsub test_shm_pubsub.cpp)
    target_link_libraries(test_shm_pubsub PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_shm_pubsub COMMAND test_shm_pubsub)

    add_executable(test_qos_pubsub test_qos_pubsub.cpp)
    target_link_libraries(test_qos_pubsub PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_qos_pubsub COMMAND test_qos_pubsub)
else()
    message(WARNING "gtest not found, skipping tests")
endif()
