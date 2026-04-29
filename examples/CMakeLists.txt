# examples 模块:示例程序

# publisher_demo
add_executable(publisher_demo publisher_demo.cpp)
target_link_libraries(publisher_demo PRIVATE mm_core)

# subscriber_demo
add_executable(subscriber_demo subscriber_demo.cpp)
target_link_libraries(subscriber_demo PRIVATE mm_core)

add_executable(queue_test queue_test.cpp)
target_link_libraries(queue_test PRIVATE mm_common)

add_executable(tcp_server_demo tcp_server_demo.cpp)
target_link_libraries(tcp_server_demo PRIVATE mm_transport)

add_executable(tcp_client_demo tcp_client_demo.cpp)
target_link_libraries(tcp_client_demo PRIVATE mm_transport)