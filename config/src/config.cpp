#include "config/config.h"

#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>

namespace mm {

namespace {

std::string trim(const std::string& s) {
    std::size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
        ++first;
    }

    std::size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) {
        --last;
    }

    return s.substr(first, last - first);
}

std::string strip_comment(const std::string& line) {
    const auto pos = line.find('#');
    if (pos == std::string::npos) {
        return line;
    }
    return line.substr(0, pos);
}

bool parse_bool(const std::string& value, bool* out) {
    if (value == "true") {
        *out = true;
        return true;
    }
    if (value == "false") {
        *out = false;
        return true;
    }
    return false;
}

bool parse_unsigned(const std::string& value, unsigned long long max_value, unsigned long long* out) {
    if (value.empty()) {
        return false;
    }

    unsigned long long parsed = 0;
    for (char c : value) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        const auto digit = static_cast<unsigned long long>(c - '0');
        if (parsed > (max_value - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }

    *out = parsed;
    return true;
}

bool parse_u16(const std::string& value, uint16_t* out) {
    unsigned long long parsed = 0;
    if (!parse_unsigned(value, std::numeric_limits<uint16_t>::max(), &parsed)) {
        return false;
    }
    *out = static_cast<uint16_t>(parsed);
    return true;
}

bool parse_u32(const std::string& value, uint32_t* out) {
    unsigned long long parsed = 0;
    if (!parse_unsigned(value, std::numeric_limits<uint32_t>::max(), &parsed)) {
        return false;
    }
    *out = static_cast<uint32_t>(parsed);
    return true;
}

ConfigParseResult fail(const MiddlewareConfig& cfg, const std::string& msg) {
    ConfigParseResult result;
    result.ok = false;
    result.config = cfg;
    result.error = msg;
    return result;
}

}  // namespace

MiddlewareConfig default_config() {
    MiddlewareConfig cfg;
    cfg.qos.reliability = Qos::Reliability::BEST_EFFORT;
    cfg.qos.history = Qos::History::KEEP_LAST;
    cfg.qos.depth = 16;
    return cfg;
}

ConfigParseResult parse_config_text(const std::string& text) {
    MiddlewareConfig cfg = default_config();
    std::istringstream in(text);
    std::string section;
    std::string line;
    int line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        line = trim(strip_comment(line));
        if (line.empty()) {
            continue;
        }

        if (line.back() == ':') {
            section = trim(line.substr(0, line.size() - 1));
            continue;
        }

        const auto pos = line.find(':');
        if (pos == std::string::npos) {
            return fail(cfg, "line " + std::to_string(line_no) + ": expected key: value");
        }

        const std::string key = trim(line.substr(0, pos));
        const std::string value = trim(line.substr(pos + 1));

        if (section == "node" && key == "name") {
            cfg.node.name = value;
        } else if (section == "transport" && key == "enable_shm") {
            bool parsed = false;
            if (!parse_bool(value, &parsed)) {
                return fail(cfg, "line " + std::to_string(line_no) + ": invalid enable_shm");
            }
            cfg.transport.enable_shm = parsed;
        } else if (section == "discovery" && key == "group") {
            cfg.discovery.group = value;
        } else if (section == "discovery" && key == "port") {
            uint16_t parsed = 0;
            if (!parse_u16(value, &parsed)) {
                return fail(cfg, "line " + std::to_string(line_no) + ": invalid port");
            }
            cfg.discovery.port = parsed;
        } else if (section == "qos" && key == "reliability") {
            if (value == "best_effort") {
                cfg.qos.reliability = Qos::Reliability::BEST_EFFORT;
            } else if (value == "reliable") {
                cfg.qos.reliability = Qos::Reliability::RELIABLE;
            } else {
                return fail(cfg, "line " + std::to_string(line_no) + ": invalid reliability");
            }
        } else if (section == "qos" && key == "history") {
            if (value == "keep_last") {
                cfg.qos.history = Qos::History::KEEP_LAST;
            } else if (value == "keep_all") {
                cfg.qos.history = Qos::History::KEEP_ALL;
            } else {
                return fail(cfg, "line " + std::to_string(line_no) + ": invalid history");
            }
        } else if (section == "qos" && key == "depth") {
            uint32_t parsed = 0;
            if (!parse_u32(value, &parsed)) {
                return fail(cfg, "line " + std::to_string(line_no) + ": invalid depth");
            }
            cfg.qos.depth = parsed;
        }
    }

    ConfigParseResult result;
    result.ok = true;
    result.config = cfg;
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
