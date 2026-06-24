#pragma once

#include <string>

namespace mm {

enum class CliCommandKind {
    HELP,
    ERROR,
    TOPIC_LIST,
    TOPIC_ECHO,
    TOPIC_HZ,
};

struct CliCommand {
    CliCommandKind kind = CliCommandKind::HELP;
    int exit_code = 0;
    std::string message;
    std::string config_path;
    std::string topic;
    std::string type_name;
    int wait_ms = 1500;
    int count = 0;
    int window = 10;
};

CliCommand parse_cli_args(int argc, char** argv);
std::string cli_usage();

}  // namespace mm
