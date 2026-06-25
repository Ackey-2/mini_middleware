#include "cli/args.h"
#include "cli/topic_commands.h"
#include "config/config.h"
#include "core/node.h"

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    auto cmd = mm::parse_cli_args(argc, argv);

    if (cmd.kind == mm::CliCommandKind::HELP) {
        std::cout << cmd.message;
        return 0;
    }

    if (cmd.kind == mm::CliCommandKind::ERROR) {
        std::cerr << cmd.message;
        return cmd.exit_code;
    }

    mm::MiddlewareConfig cfg = mm::default_config();
    if (!cmd.config_path.empty()) {
        auto result = mm::load_config_file(cmd.config_path);
        if (!result.ok) {
            std::cerr << result.error << '\n';
            return 2;
        }
        cfg = result.config;
    }

    mm::NodeOptions opts;
    opts.enable_shm = cfg.transport.enable_shm;
    opts.discovery_group = cfg.discovery.group;
    opts.discovery_port = cfg.discovery.port;

    mm::Node node(cfg.node.name, opts);

    switch (cmd.kind) {
    case mm::CliCommandKind::TOPIC_LIST:
        std::this_thread::sleep_for(std::chrono::milliseconds(cmd.wait_ms));
        return mm::run_topic_list(node.discovery(), std::cout);
    case mm::CliCommandKind::TOPIC_ECHO:
        return mm::run_topic_echo(node, cmd.topic, cmd.type_name, cmd.count, std::cout);
    case mm::CliCommandKind::TOPIC_HZ:
        return mm::run_topic_hz(node, cmd.topic, cmd.type_name, cmd.window, cmd.count, std::cout);
    case mm::CliCommandKind::HELP:
    case mm::CliCommandKind::ERROR:
        break;
    }

    return 2;
}
