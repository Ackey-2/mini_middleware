add_library(mm_cli_lib STATIC
    src/args.cpp
    src/message_format.cpp
)

target_include_directories(mm_cli_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mm_cli_lib PUBLIC
    mm_core
    mm_config
    mm_proto
)
