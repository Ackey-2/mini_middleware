#include "core/publisher.h"
#include "core/local_bus.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace mm;

class FakeSink : public ISink {
public:
    void enqueue(const std::string& bytes) override { received.push_back(bytes); }
    std::vector<std::string> received;
};

TEST(Publisher, SerializesAndPublishesToBus) {
    auto bus = std::make_shared<LocalBus>();
    auto sink = std::make_shared<FakeSink>();
    // 用 StringMsg 的全名订阅,类型要和 Publisher 一致
    bus->subscribe("/chatter", mm::StringMsg().GetDescriptor()->full_name(), sink);

    Publisher<mm::StringMsg> pub("/chatter", bus);
    mm::StringMsg msg;
    msg.set_data("hi");
    EXPECT_TRUE(pub.publish(msg));

    ASSERT_EQ(sink->received.size(), 1u);
    mm::StringMsg parsed;
    ASSERT_TRUE(parsed.ParseFromString(sink->received[0]));
    EXPECT_EQ(parsed.data(), "hi");
}

TEST(Publisher, ReportsTopic) {
    auto bus = std::make_shared<LocalBus>();
    Publisher<mm::StringMsg> pub("/chatter", bus);
    EXPECT_EQ(pub.topic(), "/chatter");
}
