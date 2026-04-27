#include "common/blocking_queue.h"
#include "common/logger.h"
#include <thread>
#include <vector>

int main() {
    mm::BlockingQueue<int> q;

    // 生产者:发 20 个数字
    std::thread producer([&q] {
        for (int i = 0; i < 20; ++i) {
            q.push(i);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        LOG_INFO("producer done, closing queue");
        q.close();
    });

    // 2 个消费者:抢着消费
    std::vector<std::thread> consumers;
    for (int id = 0; id < 2; ++id) {
        consumers.emplace_back([&q, id] {//在容器的末尾直接“原地构造”一个元素
            int val;
            while (q.pop(val)) {
                LOG_INFO("consumer {} got {}", id, val);
            }
            LOG_INFO("consumer {} exiting", id);
        });
    }

    producer.join();
    for (auto& t : consumers) t.join();

    LOG_INFO("all done, final queue size: {}", q.size());
    return 0;
}