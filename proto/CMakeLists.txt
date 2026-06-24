# ═══════════════════════════════════════════════════════════════
# proto 模块:protobuf 消息定义
# 自动调用 protoc 编译 .proto → .pb.h / .pb.cc
# ═══════════════════════════════════════════════════════════════

# 找 protobuf(顶层 CMakeLists 已经 find_package 过)
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/messages.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/data.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/rpc.proto
)

# CMake 内置函数:接收 .proto 文件列表,生成 .pb.h/.pb.cc
# 把生成的文件路径放在 PROTO_SRCS / PROTO_HDRS 里
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# 用生成的 .pb.cc 编译成库
add_library(mm_proto STATIC
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

# 暴露生成文件所在的目录(让别人能 #include "messages.pb.h")
target_include_directories(mm_proto PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}      # 生成的 .pb.h 在这里
)

# 链接 protobuf 运行时库
target_link_libraries(mm_proto PUBLIC
    protobuf::libprotobuf
)
