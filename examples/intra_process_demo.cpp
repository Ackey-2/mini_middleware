#include "core/node.h"
#include "common/logger.h"
#include "messages.pb.h"

#include <chrono>
#include <thread>

int main() {
    mm::Node node("demo_node");

    // 两个订阅者订阅同一个 topic
    auto sub1 = node.create_subscriber<mm::StringMsg>(
        "/chatter", [](const mm::StringMsg& m) {
            LOG_INFO("[sub1] got: {}", m.data());
        });
    auto sub2 = node.create_subscriber<mm::StringMsg>(
        "/chatter", [](const mm::StringMsg& m) {
            LOG_INFO("[sub2] got: {}", m.data());
        });

    auto pub = node.create_publisher<mm::StringMsg>("/chatter");

    for (int i = 0; i < 5; ++i) {
        mm::StringMsg msg;
        msg.set_data("hello " + std::to_string(i));
        pub->publish(msg);
        LOG_INFO("[pub] sent: {}", msg.data());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 给订阅者工作线程一点时间把队列里的消息处理完
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return 0;
}
