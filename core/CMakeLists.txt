# core 模块:Node / Publisher / Subscriber
add_library(mm_core STATIC
    src/local_bus.cpp
    src/node.cpp
)

target_include_directories(mm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# core 依赖 transport / proto / common,并用到 std::thread
target_link_libraries(mm_core PUBLIC
    mm_transport
    mm_proto
    mm_common
    Threads::Threads
)
