add_library(mm_config STATIC
    src/config.cpp
)

target_include_directories(mm_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mm_config PUBLIC
    mm_common
)
