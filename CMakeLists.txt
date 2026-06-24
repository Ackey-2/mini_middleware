# ═══════════════════════════════════════════════════════════════
# 顶层 CMakeLists.txt
# 职责:声明项目、设置全局编译选项、添加所有子模块
# ═══════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.15)
project(mini_middleware
        VERSION 0.1.0
        LANGUAGES CXX)

# ── 编译选项 ────────────────────────────────────────────────
# C++17 是个安全的选择,够用,兼容性也好
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)         # 关掉 GNU 扩展,代码更可移植

# 警告全开(发现 bug 越早越好)
add_compile_options(-Wall -Wextra -Wpedantic)

# Debug 模式默认开,方便 gdb 调试
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 让编译生成 compile_commands.json,VSCode/clangd 需要
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 第三方依赖 ──────────────────────────────────────────────
find_package(Threads REQUIRED)         # pthread,Day 2 用过
find_package(Protobuf REQUIRED)        # Day 5 用过

# ── 子模块 ──────────────────────────────────────────────────
# 顺序很重要:被依赖的先 add
add_subdirectory(common)               # 工具库,谁都依赖它
add_subdirectory(config)               # CLI/config 默认配置
add_subdirectory(proto)                # 消息定义
add_subdirectory(transport)            # 传输层
add_subdirectory(discovery)            # 发现层
add_subdirectory(core)                 # Pub/Sub/Node
add_subdirectory(cli)                  # 命令行工具
add_subdirectory(examples)             # 示例程序
enable_testing()   
add_subdirectory(tests)              # 测试,等有 gtest 再开

# ── 提示信息 ────────────────────────────────────────────────
message(STATUS "═══════════════════════════════════════")
message(STATUS "  ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "═══════════════════════════════════════")
