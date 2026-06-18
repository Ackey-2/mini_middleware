# transport 模块:传输层(TCP / UDP / 共享内存)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp "// auto-generated\n")


add_library(mm_transport STATIC
    src/frame_codec.cpp
    src/tcp_server_transport.cpp
    src/tcp_client_transport.cpp
    src/shm_segment.cpp
)
target_include_directories(mm_transport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# transport 依赖 common(用 logger、queue 等);rt 提供 shm_open/shm_unlink
target_link_libraries(mm_transport PUBLIC
    mm_common
    mm_proto
    rt
)

