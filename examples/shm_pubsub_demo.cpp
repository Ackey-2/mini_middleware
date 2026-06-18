#include "core/node.h"
#include "messages.pb.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// 跨进程共享内存零拷贝 pub/sub 演示。两个进程在同一台机器上 → 数据面自动走 SHM。
//
// 用法(开两个终端):
//   ./shm_pubsub_demo listener    # 进程1:订阅并打印
//   ./shm_pubsub_demo talker      # 进程2:每秒发布一条
//
// 运行时可观察共享内存段:
//   ls -l /dev/shm/mm.*           # 发布者在线时可见,退出后被 unlink
// 若进程被 kill -9 残留段,手动清理:rm -f /dev/shm/mm.*
int main(int argc, char** argv) {
    std::string role = (argc > 1) ? argv[1] : "talker";
    mm::Node node(role);   // 默认启用 SHM:同机自动零拷贝
    const std::string topic = "/chatter";

    if (role == "listener") {
        auto sub = node.create_subscriber<mm::StringMsg>(
            topic, [](const mm::StringMsg& m) {
                std::cout << "[listener] recv: " << m.data() << std::endl;
            });
        std::cout << "[listener] waiting on " << topic << " (shared memory) ..." << std::endl;
        while (true) std::this_thread::sleep_for(1s);
    } else {
        auto pub = node.create_publisher<mm::StringMsg>(topic);
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
