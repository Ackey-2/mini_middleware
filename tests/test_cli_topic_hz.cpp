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

TEST(CliTopicHz, PrintsAverageRate) {
    Qos reliable;
    reliable.reliability = Qos::Reliability::RELIABLE;

    Node talker("cli_hz_talker");
    auto listener = std::make_shared<Node>("cli_hz_listener");
    talker.discovery().set_timing(80ms, 5000ms);
    listener->discovery().set_timing(80ms, 5000ms);

    auto pub = talker.create_publisher<mm::StringMsg>("/cli_hz", reliable);

    auto out = std::make_shared<std::ostringstream>();
    auto code = std::make_shared<std::atomic<int>>(-1);
    std::thread runner([listener, out, code] {
        code->store(run_topic_hz(*listener, "/cli_hz", "mm.StringMsg", 5, 4, *out));
    });

    std::this_thread::sleep_for(100ms);
    for (int i = 0; i < 300; ++i) {
        mm::StringMsg msg;
        msg.set_data("tick");
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
        if (code->load() == 0) {
            break;
        }
    }

    if (code->load() != 0) {
        runner.detach();
        FAIL() << "run_topic_hz did not finish after repeated publishes";
    }
    runner.join();

    EXPECT_EQ(code->load(), 0);
    EXPECT_NE(out->str().find("average rate:"), std::string::npos);
}
