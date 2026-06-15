# discovery 模块:UDP 多播服务发现
add_library(mm_discovery STATIC
    src/udp_multicast.cpp
    src/endpoint_matcher.cpp
    src/discovery_agent.cpp
)

target_include_directories(mm_discovery PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mm_discovery PUBLIC
    mm_proto
    mm_common
    Threads::Threads
)
