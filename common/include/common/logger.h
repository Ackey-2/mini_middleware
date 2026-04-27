#pragma once

#include <cstdio>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <chrono>
#include <iomanip>

namespace mm {

enum class LogLevel { INFO, WARN, ERROR };

class Logger {
public:
    // 单例:全局唯一一个 Logger
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    // 真正的输出函数,被宏调用
    void log(LogLevel level, const char* file, int line, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx_);

        // 时间戳
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) % 1000;
        auto t = std::chrono::system_clock::to_time_t(now);

        std::tm tm{};
        localtime_r(&t, &tm);

        // 颜色码
        const char* color = "";
        const char* tag = "";
        switch (level) {
            case LogLevel::INFO:  color = "\033[32m"; tag = "INFO ";  break;  // 绿
            case LogLevel::WARN:  color = "\033[33m"; tag = "WARN ";  break;  // 黄
            case LogLevel::ERROR: color = "\033[31m"; tag = "ERROR"; break;   // 红
        }
        const char* reset = "\033[0m";

        // 提取文件名(去掉路径)
        const char* basename = file;
        for (const char* p = file; *p; ++p) {
            if (*p == '/') basename = p + 1;
        }

        // 输出格式:[时间] [级别] [文件:行] 消息
        std::fprintf(stderr,
                     "%s[%02d:%02d:%02d.%03d] [%s] [%s:%d] %s%s\n",
                     color,
                     tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms.count(),
                     tag,
                     basename, line,
                     msg.c_str(),
                     reset);
    }

private:
    Logger() = default;
    std::mutex mtx_;
};

// ─────────────────────────────────────────────────────────
// 简易 fmt-style 字符串拼接
// 用法:format_string("hello {}, age {}", "alice", 30)
//       → "hello alice, age 30"
// ─────────────────────────────────────────────────────────

// 递归终止:没有更多参数
inline void format_helper(std::ostringstream& oss, const std::string& fmt, size_t pos) {
    oss << fmt.substr(pos);
}

// 递归展开:每次替换一个 {}
template <typename T, typename... Args>
void format_helper(std::ostringstream& oss, const std::string& fmt, size_t pos,
                   T&& value, Args&&... args) {
    size_t placeholder = fmt.find("{}", pos);
    if (placeholder == std::string::npos) {
        oss << fmt.substr(pos);
        return;
    }
    oss << fmt.substr(pos, placeholder - pos);
    oss << std::forward<T>(value);
    format_helper(oss, fmt, placeholder + 2, std::forward<Args>(args)...);
}

template <typename... Args>
std::string format_string(const std::string& fmt, Args&&... args) {
    std::ostringstream oss;
    format_helper(oss, fmt, 0, std::forward<Args>(args)...);
    return oss.str();
}

}  // namespace mm

// ─────────────────────────────────────────────────────────
// 宏:用户接口,自动捕获 __FILE__ 和 __LINE__
// ─────────────────────────────────────────────────────────
#define LOG_INFO(...) \
    mm::Logger::instance().log(mm::LogLevel::INFO, __FILE__, __LINE__, \
                                mm::format_string(__VA_ARGS__))

#define LOG_WARN(...) \
    mm::Logger::instance().log(mm::LogLevel::WARN, __FILE__, __LINE__, \
                                mm::format_string(__VA_ARGS__))

#define LOG_ERROR(...) \
    mm::Logger::instance().log(mm::LogLevel::ERROR, __FILE__, __LINE__, \
                                mm::format_string(__VA_ARGS__))