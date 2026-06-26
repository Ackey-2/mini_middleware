add_library(mm_bench_lib STATIC
    src/bench_args.cpp
)

target_include_directories(mm_bench_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
