#include "config/config.h"

#include <fstream>
#include <sstream>

namespace mm {

MiddlewareConfig default_config() {
    MiddlewareConfig cfg;
    cfg.qos.reliability = Qos::Reliability::BEST_EFFORT;
    cfg.qos.history = Qos::History::KEEP_LAST;
    cfg.qos.depth = 16;
    return cfg;
}

ConfigParseResult parse_config_text(const std::string&) {
    ConfigParseResult result;
    result.config = default_config();
    return result;
}

ConfigParseResult load_config_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        ConfigParseResult result;
        result.ok = false;
        result.config = default_config();
        result.error = "failed to open config file: " + path;
        return result;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_config_text(ss.str());
}

}  // namespace mm
