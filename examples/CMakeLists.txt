# examples 模块:示例程序



# subscriber_demo
add_executable(subscriber_demo subscriber_demo.cpp)
target_link_libraries(subscriber_demo PRIVATE mm_core)

add_executable(queue_test queue_test.cpp)
target_link_libraries(queue_test PRIVATE mm_common)

add_executable(tcp_server_demo tcp_server_demo.cpp)
target_link_libraries(tcp_server_demo PRIVATE mm_transport)

add_executable(tcp_client_demo tcp_client_demo.cpp)
target_link_libraries(tcp_client_demo PRIVATE mm_transport)

add_executable(intra_process_demo intra_process_demo.cpp)
target_link_libraries(intra_process_demo PRIVATE mm_core)

add_executable(discovery_demo discovery_demo.cpp)
target_link_libraries(discovery_demo PRIVATE mm_core)
