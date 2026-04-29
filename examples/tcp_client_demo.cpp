#include "transport/tcp_client_transport.h"
#include "common/logger.h"
#include <thread>
#include <chrono>

int main() {
    mm::TcpClientTransport client("127.0.0.1", 9000);

    client.on_message([](const std::string& payload) {
        LOG_INFO("client received: {}", payload);
    });

    if (!client.start()) {
        LOG_ERROR("start failed");
        return 1;
    }

    // 等连接建立
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 发 5 条消息
    for (int i = 0; i < 5; ++i) {
        std::string msg = "hello " + std::to_string(i);
        if (client.send(msg)) {
            LOG_INFO("sent: {}", msg);
        } else {
            LOG_WARN("send failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    client.stop();
    return 0;
}