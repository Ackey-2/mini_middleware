#include "cli/args.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

namespace mm {
namespace {

CliCommand help_command() {
    CliCommand cmd;
    cmd.kind = CliCommandKind::HELP;
    cmd.exit_code = 0;
    cmd.message = cli_usage();
    return cmd;
}

CliCommand error_command(const std::string& message) {
    CliCommand cmd;
    cmd.kind = CliCommandKind::ERROR;
    cmd.exit_code = 2;
    cmd.message = message + "\n\n" + cli_usage();
    return cmd;
}

bool parse_non_negative_int(const std::string& text, int* out) {
    if (text.empty() || text[0] == '-') {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    long value = std::strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' || value > INT_MAX) {
        return false;
    }

    *out = static_cast<int>(value);
    return true;
}

bool read_option_value(int argc, char** argv, int index, std::string* value, CliCommand* error) {
    if (index + 1 >= argc) {
        *error = error_command(std::string(argv[index]) + " requires a value");
        return false;
    }
    *value = argv[index + 1];
    return true;
}

bool apply_option(CliCommand* cmd, const std::string& name, const std::string& value, CliCommand* error) {
    if (name == "--config") {
        cmd->config_path = value;
        return true;
    }
    if (name == "--type") {
        cmd->type_name = value;
        return true;
    }

    int parsed = 0;
    if (name == "--wait-ms") {
        if (!parse_non_negative_int(value, &parsed)) {
            *error = error_command("--wait-ms must be a non-negative integer");
            return false;
        }
        cmd->wait_ms = parsed;
        return true;
    }
    if (name == "--count") {
        if (!parse_non_negative_int(value, &parsed)) {
            *error = error_command("--count must be a non-negative integer");
            return false;
        }
        cmd->count = parsed;
        return true;
    }
    if (name == "--window") {
        if (!parse_non_negative_int(value, &parsed) || parsed < 2) {
            *error = error_command("--window must be an integer >= 2");
            return false;
        }
        cmd->window = parsed;
        return true;
    }

    *error = error_command("unknown option: " + name);
    return false;
}

bool parse_options(int argc, char** argv, int start, CliCommand* cmd, CliCommand* error) {
    for (int i = start; i < argc; i += 2) {
        std::string name = argv[i];
        if (name.rfind("--", 0) != 0) {
            *error = error_command("unexpected argument: " + name);
            return false;
        }

        std::string value;
        if (!read_option_value(argc, argv, i, &value, error)) {
            return false;
        }
        if (!apply_option(cmd, name, value, error)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string cli_usage() {
    return "Usage:\n"
           "  mm --help\n"
           "  mm topic --help\n"
           "  mm topic list [--config PATH] [--wait-ms MS]\n"
           "  mm topic echo TOPIC --type TYPE [--config PATH] [--count N] [--wait-ms MS]\n"
           "  mm topic hz TOPIC --type TYPE [--config PATH] [--window N] [--wait-ms MS]\n";
}

CliCommand parse_cli_args(int argc, char** argv) {
    if (argc <= 1 || std::string(argv[1]) == "--help") {
        return help_command();
    }

    if (std::string(argv[1]) != "topic") {
        return error_command("unknown command: " + std::string(argv[1]));
    }

    if (argc == 3 && std::string(argv[2]) == "--help") {
        return help_command();
    }
    if (argc < 3) {
        return error_command("missing topic command");
    }

    const std::string subcommand = argv[2];
    CliCommand cmd;
    if (subcommand == "list") {
        cmd.kind = CliCommandKind::TOPIC_LIST;
        if (!parse_options(argc, argv, 3, &cmd, &cmd)) {
            return cmd;
        }
        return cmd;
    }

    if (subcommand == "echo" || subcommand == "hz") {
        if (argc < 4 || std::string(argv[3]).rfind("--", 0) == 0) {
            return error_command(subcommand + " requires a topic");
        }

        cmd.kind = subcommand == "echo" ? CliCommandKind::TOPIC_ECHO : CliCommandKind::TOPIC_HZ;
        cmd.topic = argv[3];
        if (!parse_options(argc, argv, 4, &cmd, &cmd)) {
            return cmd;
        }
        if (cmd.type_name.empty()) {
            return error_command(subcommand + " requires --type");
        }
        return cmd;
    }

    return error_command("unknown topic command: " + subcommand);
}

}  // namespace mm
