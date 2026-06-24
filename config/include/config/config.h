#pragma once

#include "common/qos.h"

#include <cstdint>
#include <string>

namespace mm {

struct NodeConfig {
    std::string name = "mm_cli";
};

struct TransportConfig {
    bool enable_shm = true;
};

struct DiscoveryConfig {
    std::string group = "239.255.0.1";
    uint16_t port = 7400;
};

struct MiddlewareConfig {
    NodeConfig node;
    TransportConfig transport;
    DiscoveryConfig discovery;
    Qos qos;
};

struct ConfigParseResult {
    bool ok = true;
    MiddlewareConfig config;
    std::string error;
};

MiddlewareConfig default_config();
ConfigParseResult parse_config_text(const std::string& text);
ConfigParseResult load_config_file(const std::string& path);

}  // namespace mm
