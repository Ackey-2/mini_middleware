#include "transport/tcp_server_transport.h"
#include <gtest/gtest.h>

using namespace mm;

TEST(TcpServerTransport, EphemeralPortAssigned) {
    TcpServerTransport server(0);          // 端口 0 => 内核分配
    ASSERT_TRUE(server.start());
    EXPECT_GT(server.local_port(), 0);     // start 后应是真实端口
    server.stop();
}
