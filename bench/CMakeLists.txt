add_library(mm_bench_lib STATIC
    src/bench_args.cpp
    src/message_codec.cpp
    src/stats.cpp
)

target_include_directories(mm_bench_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mm_bench_lib PRIVATE
    mm_core
    mm_proto
)

add_executable(mm_bench
    src/main.cpp
)

target_link_libraries(mm_bench PRIVATE
    mm_bench_lib
    mm_core
    mm_proto
)
