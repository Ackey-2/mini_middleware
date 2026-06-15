#include "core/subscriber.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace mm;
using namespace std::chrono_literals;

// 把一个 StringMsg 序列化成字节
static std::string make_bytes(const std::string& data) {
    mm::StringMsg m;
    m.set_data(data);
    std::string out;
    m.SerializeToString(&out);
    return out;
}

TEST(Subscriber, DeliversParsedMessageToCallback) {
    std::atomic<int> count{0};
    std::string last;
    auto sub = std::make_shared<Subscriber<mm::StringMsg>>(
        "/t", [&](const mm::StringMsg& m) { last = m.data(); ++count; });

    sub->enqueue(make_bytes("hello"));

    // 工作线程异步处理,轮询等待最多 1s
    for (int i = 0; i < 100 && count.load() == 0; ++i) std::this_thread::sleep_for(10ms);

    EXPECT_EQ(count.load(), 1);
    EXPECT_EQ(last, "hello");
}

TEST(Subscriber, ProcessesMultipleInOrder) {
    std::vector<std::string> got;
    std::mutex m;
    std::atomic<int> count{0};
    auto sub = std::make_shared<Subscriber<mm::StringMsg>>(
        "/t", [&](const mm::StringMsg& msg) {
            std::lock_guard<std::mutex> lk(m);
            got.push_back(msg.data());
            ++count;
        });

    for (int i = 0; i < 5; ++i) sub->enqueue(make_bytes(std::to_string(i)));
    for (int i = 0; i < 100 && count.load() < 5; ++i) std::this_thread::sleep_for(10ms);

    ASSERT_EQ(count.load(), 5);
    std::vector<std::string> expected{"0", "1", "2", "3", "4"};
    EXPECT_EQ(got, expected);
}

TEST(Subscriber, DestructorJoinsCleanly) {
    // 创建后立刻销毁,不应挂起(队列 close + 线程 join)
    auto sub = std::make_shared<Subscriber<mm::StringMsg>>(
        "/t", [](const mm::StringMsg&) {});
    sub.reset();
    SUCCEED();
}
