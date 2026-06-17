#include "core/remote_sink.h"
#include "data.pb.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace mm;

// 假传输:捕获 send 的字节,不做网络
class FakeTransport : public Transport {
public:
    bool start() override { return true; }
    void stop() override {}
    void on_message(MessageCallback) override {}   // RemoteSink 不注册回调,空实现
    bool send(const std::string& payload) override { sent.push_back(payload); return true; }
    std::vector<std::string> sent;
};

TEST(RemoteSink, WrapsBytesIntoDataMessage) {
    auto t = std::make_shared<FakeTransport>();
    RemoteSink sink("/scan", t);

    sink.enqueue("hello");

    ASSERT_EQ(t->sent.size(), 1u);
    DataMessage m;
    ASSERT_TRUE(m.ParseFromString(t->sent[0]));
    EXPECT_EQ(m.topic(), "/scan");
    EXPECT_EQ(m.payload(), "hello");
}
