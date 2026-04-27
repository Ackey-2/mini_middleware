#include "common/logger.h"
#include <thread>
#include <vector>

int main() {
    LOG_INFO("publisher_demo started");
    LOG_INFO("topic={}, port={}", "/chatter", 9000);
    LOG_WARN("this is a warning");
    LOG_ERROR("this is an error code={}", 42);

    // 测试多线程并发输出
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([i] {
            for (int j = 0; j < 5; ++j) {
                LOG_INFO("thread {} message {}", i, j);
            }
        });
    }
    for (auto& t : threads) t.join();

    LOG_INFO("publisher_demo exiting");
    return 0;
}