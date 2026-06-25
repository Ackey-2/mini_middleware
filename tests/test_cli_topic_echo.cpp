#include "cli/topic_commands.h"
#include "common/qos.h"
#include "core/node.h"
#include "messages.pb.h"

#include <gtest/gtest.h>

#include <chrono>
#include <atomic>
#include <memory>
#include <sstream>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

TEST(CliTopicEcho, PrintsFormattedMessage) {
    Qos reliable;
    reliable.reliability = Qos::Reliability::RELIABLE;

    Node talker("cli_echo_talker");
    auto listener = std::make_shared<Node>("cli_echo_listener");
    talker.discovery().set_timing(80ms, 5000ms);
    listener->discovery().set_timing(80ms, 5000ms);

    auto pub = talker.create_publisher<mm::StringMsg>("/cli_echo", reliable);

    auto out = std::make_shared<std::ostringstream>();
    auto code = std::make_shared<std::atomic<int>>(-1);
    std::thread runner([listener, out, code] {
        code->store(run_topic_echo(*listener, "/cli_echo", "mm.StringMsg", 1, *out));
    });

    std::this_thread::sleep_for(100ms);
    for (int i = 0; i < 300; ++i) {
        mm::StringMsg msg;
        msg.set_data("hello");
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
        if (code->load() == 0) {
            break;
        }
    }

    if (code->load() != 0) {
        runner.detach();
        FAIL() << "run_topic_echo did not finish after repeated publishes";
    }
    runner.join();

    EXPECT_EQ(code->load(), 0);
    EXPECT_NE(out->str().find("data: hello"), std::string::npos);
}

TEST(CliTopicEcho, IgnoresMessagesAfterCountReached) {
    Qos reliable;
    reliable.reliability = Qos::Reliability::RELIABLE;

    Node talker("cli_echo_late_talker");
    auto listener = std::make_shared<Node>("cli_echo_late_listener");
    talker.discovery().set_timing(80ms, 5000ms);
    listener->discovery().set_timing(80ms, 5000ms);

    auto pub = talker.create_publisher<mm::StringMsg>("/cli_echo_late", reliable);

    auto out = std::make_shared<std::ostringstream>();
    auto code = std::make_shared<std::atomic<int>>(-1);
    std::thread runner([listener, out, code] {
        code->store(run_topic_echo(*listener, "/cli_echo_late", "mm.StringMsg", 1, *out));
    });

    std::this_thread::sleep_for(100ms);
    for (int i = 0; i < 300; ++i) {
        mm::StringMsg msg;
        msg.set_data("first");
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
        if (code->load() == 0) {
            break;
        }
    }

    if (code->load() != 0) {
        runner.detach();
        FAIL() << "run_topic_echo did not finish after repeated publishes";
    }
    runner.join();

    const std::string output_after_return = out->str();
    EXPECT_NE(output_after_return.find("data: first"), std::string::npos);

    for (int i = 0; i < 10; ++i) {
        mm::StringMsg msg;
        msg.set_data("late");
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
    }

    EXPECT_EQ(out->str(), output_after_return);
}
