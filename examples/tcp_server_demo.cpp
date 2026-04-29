#include "transport/tcp_server_transport.h"
#include "transport/frame_codec.h"
#include "common/logger.h"
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
std::atomic<bool> g_stop{false};

void signal_handler(int) { g_stop = true; }

int main() {
    std::signal(SIGINT, signal_handler);

    mm::TcpServerTransport server(9000);
    std::cout<<"------------"<<std::endl;
    server.on_message([](const std::string& payload) {
        LOG_INFO("received message: '{}' (size={})", payload, payload.size());
    });

    if (!server.start()) {
        LOG_ERROR("server failed to start");
        return 1;
    }

    LOG_INFO("server running. Press Ctrl+C to stop.");

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("stopping server...");
    server.stop();
    return 0;
}