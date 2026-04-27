# core 模块:Node / Publisher / Subscriber
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp "// auto-generated\n")

add_library(mm_core STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp
)

target_include_directories(mm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# core 依赖 transport
target_link_libraries(mm_core PUBLIC
    mm_transport
)