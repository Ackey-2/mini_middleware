# examples 模块:示例程序

# publisher_demo
add_executable(publisher_demo publisher_demo.cpp)
target_link_libraries(publisher_demo PRIVATE mm_core)

# subscriber_demo
add_executable(subscriber_demo subscriber_demo.cpp)
target_link_libraries(subscriber_demo PRIVATE mm_core)