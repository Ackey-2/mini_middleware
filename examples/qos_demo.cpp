#include "core/node.h"
#include "common/qos.h"
#include "messages.pb.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// QoS 演示:可靠性协商 + 选路。两个进程同机:
//   - reliable 模式:RELIABLE 发布/订阅 → 走 TCP(有序不丢),协商通过。
//   - 默认(best-effort)模式:同机走共享内存零拷贝。
//   - 若一端 RELIABLE、另一端 BEST_EFFORT(发布方),则 RxO 不兼容 → 不匹配。
//
// 用法(开两个终端,可加 reliable 切到 RELIABLE):
//   ./qos_demo listener [reliable]
//   ./qos_demo talker   [reliable]
int main(int argc, char** argv) {
    std::string role = (argc > 1) ? argv[1] : "talker";
    bool reliable = (argc > 2) && std::string(argv[2]) == "reliable";

    mm::Qos qos;
    qos.reliability = reliable ? mm::Qos::Reliability::RELIABLE
                               : mm::Qos::Reliability::BEST_EFFORT;
    qos.history = mm::Qos::History::KEEP_LAST;
    qos.depth = 8;

    mm::Node node(role);
    const std::string topic = "/qos_chatter";
    const char* mode = reliable ? "RELIABLE(TCP)" : "BEST_EFFORT(SHM if same host)";

    if (role == "listener") {
        auto sub = node.create_subscriber<mm::StringMsg>(
            topic, [](const mm::StringMsg& m) {
                std::cout << "[listener] recv: " << m.data() << std::endl;
            }, qos);
        std::cout << "[listener] waiting on " << topic << " qos=" << mode << " ..." << std::endl;
        while (true) std::this_thread::sleep_for(1s);
    } else {
        auto pub = node.create_publisher<mm::StringMsg>(topic, qos);
        std::cout << "[talker] publishing qos=" << mode << std::endl;
        for (int n = 0;; ++n) {
            mm::StringMsg m;
            m.set_data("msg #" + std::to_string(n));
            pub->publish(m);
            std::cout << "[talker] sent: " << m.data() << std::endl;
            std::this_thread::sleep_for(1s);
        }
    }
    return 0;
}
