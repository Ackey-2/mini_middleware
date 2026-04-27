# ═══════════════════════════════════════════════════════════════
# common 模块
# 职责:日志、线程安全队列、其他通用工具
# ═══════════════════════════════════════════════════════════════

# 现在还没有源文件,先建一个占位的 .cpp,否则 add_library 会报错
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/placeholder.cpp "// auto-generated\n")

# 把这个模块编译成静态库,名字叫 mm_common("mm" = mini_middleware 的缩写)

add_library(mm_common STATIC
    src/logger.cpp
)

# 这个模块的 include 路径
# PUBLIC:依赖 mm_common 的模块也能用这些头文件
target_include_directories(mm_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接 pthread(后面写线程安全队列要用)
target_link_libraries(mm_common PUBLIC
    Threads::Threads
)

