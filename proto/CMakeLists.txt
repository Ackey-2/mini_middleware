# proto 模块:protobuf 消息定义
# 占位,Day 8 集成 protobuf 时填充
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp "// auto-generated\n")
add_library(mm_proto STATIC ${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp)
target_include_directories(mm_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})