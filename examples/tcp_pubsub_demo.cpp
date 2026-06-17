#include "core/node.h"
#include "messages.pb.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// 用法(开两个终端):
//   ./tcp_pubsub_demo listener    # 进程1:订阅并打印
//   ./tcp_pubsub_demo talker      # 进程2:每秒发布一条
int main(int argc, char** argv) {
    std::string role = (argc > 1) ? argv[1] : "talker";
    mm::Node node(role);
    const std::string topic = "/chatter";

    if (role == "listener") {
        auto sub = node.create_subscriber<mm::StringMsg>(
            topic, [](const mm::StringMsg& m) {
                std::cout << "[listener] recv: " << m.data() << std::endl;
            });
        std::cout << "[listener] waiting on " << topic << " ..." << std::endl;
        while (true) std::this_thread::sleep_for(1s);
    } else {
        auto pub = node.create_publisher<mm::StringMsg>(topic);
        for (int n = 0; ; ++n) {
            mm::StringMsg m;
            m.set_data("msg #" + std::to_string(n));
            pub->publish(m);
            std::cout << "[talker] sent: " << m.data() << std::endl;
            std::this_thread::sleep_for(1s);
        }
    }
    return 0;
}
