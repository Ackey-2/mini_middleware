# Phase 7 CLI and Config Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `mm` command-line tool with `topic list`, `topic echo`, `topic hz`, and shared YAML-style configuration loading.

**Architecture:** Add a dependency-light `config` library that parses the project's supported YAML subset into `MiddlewareConfig`, `NodeOptions`, and `Qos`. Add a `cli` library/executable that parses argv, formats built-in protobuf messages, and runs topic commands using existing `Node`, `Publisher`, `Subscriber`, and `DiscoveryAgent` APIs. Extend `DiscoveryAgent` with a read-only endpoint snapshot and extend `Node` with `NodeOptions` so CLI config can control discovery group, discovery port, and SHM.

**Tech Stack:** C++17, CMake, Protobuf, GoogleTest, existing `mm_core`, `mm_discovery`, `mm_proto`, and `mm_common`.

**Spec:** `docs/superpowers/specs/2026-06-24-phase7-cli-config-design.md`

---

## File Structure

- Create: `config/CMakeLists.txt`
- Create: `config/include/config/config.h`
- Create: `config/src/config.cpp`
- Create: `cli/CMakeLists.txt`
- Create: `cli/include/cli/args.h`
- Create: `cli/include/cli/message_format.h`
- Create: `cli/include/cli/topic_commands.h`
- Create: `cli/src/args.cpp`
- Create: `cli/src/message_format.cpp`
- Create: `cli/src/topic_commands.cpp`
- Create: `cli/src/main.cpp`
- Modify: `CMakeLists.txt` to add `config` and `cli` subdirectories.
- Modify: `core/include/core/node.h` and `core/src/node.cpp` for `NodeOptions`.
- Modify: `discovery/include/discovery/discovery_agent.h` and `discovery/src/discovery_agent.cpp` for `DiscoveredEndpoint` snapshots.
- Modify: `tests/CMakeLists.txt` for new tests.
- Create tests: `tests/test_config_defaults.cpp`, `tests/test_config_parse.cpp`, `tests/test_config_errors.cpp`, `tests/test_cli_args.cpp`, `tests/test_message_format.cpp`, `tests/test_discovery_snapshot.cpp`, `tests/test_cli_topic_list.cpp`, `tests/test_cli_topic_echo.cpp`, `tests/test_cli_topic_hz.cpp`.
- Modify: `docs/superpowers/specs/2026-06-15-mini-middleware-roadmap.md` to mark Phase 7 complete after full verification.

---

## Task 1: Config Defaults and CMake Module

**Files:**
- Create: `config/CMakeLists.txt`
- Create: `config/include/config/config.h`
- Create: `config/src/config.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/test_config_defaults.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_config_defaults.cpp`:

```cpp
#include "config/config.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(ConfigDefaults, ProvidesCliReadyDefaults) {
    MiddlewareConfig cfg = default_config();

    EXPECT_EQ(cfg.node.name, "mm_cli");
    EXPECT_TRUE(cfg.transport.enable_shm);
    EXPECT_EQ(cfg.discovery.group, "239.255.0.1");
    EXPECT_EQ(cfg.discovery.port, 7400u);
    EXPECT_EQ(cfg.qos.reliability, Qos::Reliability::BEST_EFFORT);
    EXPECT_EQ(cfg.qos.history, Qos::History::KEEP_LAST);
    EXPECT_EQ(cfg.qos.depth, 16u);
}
```

Add `test_config_defaults` to `tests/CMakeLists.txt`, linked with `mm_config` and `GTest::gtest_main`.

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target test_config_defaults
```

Expected: configure or build fails because `config/config.h` and `mm_config` do not exist.

- [ ] **Step 3: Add config module and defaults**

Add `add_subdirectory(config)` in top-level `CMakeLists.txt` after `common` and before `core`.

Create `config/include/config/config.h`:

```cpp
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
```

Create `config/src/config.cpp`:

```cpp
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
```

Create `config/CMakeLists.txt`:

```cmake
add_library(mm_config STATIC
    src/config.cpp
)

target_include_directories(mm_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mm_config PUBLIC
    mm_common
)
```

- [ ] **Step 4: Run the test to verify it passes**

Run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target test_config_defaults
cd build && ctest -R test_config_defaults --output-on-failure
```

Expected: `test_config_defaults` passes.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt config tests/CMakeLists.txt tests/test_config_defaults.cpp
git commit -m "feat(phase7): add config defaults module"
```

---

## Task 2: YAML Subset Parser

**Files:**
- Modify: `config/src/config.cpp`
- Create: `tests/test_config_parse.cpp`
- Create: `tests/test_config_errors.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing parser tests**

Create `tests/test_config_parse.cpp`:

```cpp
#include "config/config.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(ConfigParse, AppliesYamlOverrides) {
    const std::string text =
        "node:\n"
        "  name: inspector\n"
        "transport:\n"
        "  enable_shm: false\n"
        "discovery:\n"
        "  group: 239.1.2.3\n"
        "  port: 7501\n"
        "qos:\n"
        "  reliability: reliable\n"
        "  history: keep_all\n"
        "  depth: 32\n";

    auto result = parse_config_text(text);

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.config.node.name, "inspector");
    EXPECT_FALSE(result.config.transport.enable_shm);
    EXPECT_EQ(result.config.discovery.group, "239.1.2.3");
    EXPECT_EQ(result.config.discovery.port, 7501u);
    EXPECT_EQ(result.config.qos.reliability, Qos::Reliability::RELIABLE);
    EXPECT_EQ(result.config.qos.history, Qos::History::KEEP_ALL);
    EXPECT_EQ(result.config.qos.depth, 32u);
}

TEST(ConfigParse, IgnoresBlankLinesAndComments) {
    auto result = parse_config_text(
        "# demo config\n"
        "\n"
        "qos:\n"
        "  reliability: best_effort # inline comment\n");

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.config.qos.reliability, Qos::Reliability::BEST_EFFORT);
}
```

Create `tests/test_config_errors.cpp`:

```cpp
#include "config/config.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(ConfigErrors, RejectsInvalidReliability) {
    auto result = parse_config_text("qos:\n  reliability: maybe\n");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("reliability"), std::string::npos);
}

TEST(ConfigErrors, RejectsInvalidHistory) {
    auto result = parse_config_text("qos:\n  history: ancient\n");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("history"), std::string::npos);
}

TEST(ConfigErrors, RejectsInvalidUnsignedInteger) {
    auto result = parse_config_text("discovery:\n  port: nope\n");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("port"), std::string::npos);
}
```

Add both tests to `tests/CMakeLists.txt`, linked with `mm_config`.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target test_config_parse test_config_errors
cd build && ctest -R "test_config_parse|test_config_errors" --output-on-failure
```

Expected: `test_config_parse` fails because `parse_config_text()` currently returns defaults; `test_config_errors` fails because invalid input is accepted.

- [ ] **Step 3: Implement the YAML subset parser**

Replace `parse_config_text()` in `config/src/config.cpp` with helper functions:

```cpp
namespace {

std::string trim(const std::string& s);
std::string strip_comment(const std::string& line);
bool parse_bool(const std::string& value, bool* out);
bool parse_u16(const std::string& value, uint16_t* out);
bool parse_u32(const std::string& value, uint32_t* out);

ConfigParseResult fail(const MiddlewareConfig& cfg, const std::string& msg) {
    ConfigParseResult result;
    result.ok = false;
    result.config = cfg;
    result.error = msg;
    return result;
}

}  // namespace
```

Parser algorithm:

```cpp
ConfigParseResult parse_config_text(const std::string& text) {
    MiddlewareConfig cfg = default_config();
    std::istringstream in(text);
    std::string section;
    std::string line;
    int line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        line = trim(strip_comment(line));
        if (line.empty()) continue;

        if (line.back() == ':') {
            section = trim(line.substr(0, line.size() - 1));
            continue;
        }

        auto pos = line.find(':');
        if (pos == std::string::npos) {
            return fail(cfg, "line " + std::to_string(line_no) + ": expected key: value");
        }

        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));

        if (section == "node" && key == "name") cfg.node.name = value;
        else if (section == "transport" && key == "enable_shm") {
            bool parsed = false;
            if (!parse_bool(value, &parsed)) return fail(cfg, "line " + std::to_string(line_no) + ": invalid enable_shm");
            cfg.transport.enable_shm = parsed;
        } else if (section == "discovery" && key == "group") cfg.discovery.group = value;
        else if (section == "discovery" && key == "port") {
            uint16_t parsed = 0;
            if (!parse_u16(value, &parsed)) return fail(cfg, "line " + std::to_string(line_no) + ": invalid port");
            cfg.discovery.port = parsed;
        } else if (section == "qos" && key == "reliability") {
            if (value == "best_effort") cfg.qos.reliability = Qos::Reliability::BEST_EFFORT;
            else if (value == "reliable") cfg.qos.reliability = Qos::Reliability::RELIABLE;
            else return fail(cfg, "line " + std::to_string(line_no) + ": invalid reliability");
        } else if (section == "qos" && key == "history") {
            if (value == "keep_last") cfg.qos.history = Qos::History::KEEP_LAST;
            else if (value == "keep_all") cfg.qos.history = Qos::History::KEEP_ALL;
            else return fail(cfg, "line " + std::to_string(line_no) + ": invalid history");
        } else if (section == "qos" && key == "depth") {
            uint32_t parsed = 0;
            if (!parse_u32(value, &parsed)) return fail(cfg, "line " + std::to_string(line_no) + ": invalid depth");
            cfg.qos.depth = parsed;
        }
    }

    ConfigParseResult result;
    result.ok = true;
    result.config = cfg;
    return result;
}
```

Unknown sections and keys are ignored so config files can grow later without breaking older CLIs.

- [ ] **Step 4: Run parser tests**

Run:

```bash
cmake --build build -j$(nproc) --target test_config_parse test_config_errors
cd build && ctest -R "test_config_parse|test_config_errors" --output-on-failure
```

Expected: parser and error tests pass.

- [ ] **Step 5: Commit**

```bash
git add config/src/config.cpp tests/CMakeLists.txt tests/test_config_parse.cpp tests/test_config_errors.cpp
git commit -m "feat(phase7): parse middleware yaml config"
```

---

## Task 3: Node Options and Discovery Snapshot

**Files:**
- Modify: `core/include/core/node.h`
- Modify: `core/src/node.cpp`
- Modify: `discovery/include/discovery/discovery_agent.h`
- Modify: `discovery/src/discovery_agent.cpp`
- Create: `tests/test_discovery_snapshot.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing snapshot and options test**

Create `tests/test_discovery_snapshot.cpp`:

```cpp
#include "core/node.h"
#include "messages.pb.h"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

TEST(DiscoverySnapshot, IncludesLocalEndpoint) {
    Node node("snapshot_node");
    auto pub = node.create_publisher<mm::StringMsg>("/snapshot_chatter");

    auto endpoints = node.discovery().snapshot_endpoints();

    bool found = false;
    for (const auto& ep : endpoints) {
        if (ep.local && ep.node_name == "snapshot_node" &&
            ep.endpoint.topic() == "/snapshot_chatter" &&
            ep.endpoint.kind() == EndpointInfo::PUBLISHER) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(DiscoverySnapshot, NodeOptionsConfigureDiscoveryPort) {
    NodeOptions opts;
    opts.discovery_port = 7502;
    Node talker("configured_talker", opts);
    Node listener("configured_listener", opts);
    talker.discovery().set_timing(80ms, 5000ms);
    listener.discovery().set_timing(80ms, 5000ms);
    auto pub = talker.create_publisher<mm::StringMsg>("/configured_chatter");

    bool found = false;
    for (int i = 0; i < 30 && !found; ++i) {
        auto endpoints = listener.discovery().snapshot_endpoints();
        for (const auto& ep : endpoints) {
            if (!ep.local && ep.node_name == "configured_talker" &&
                ep.endpoint.topic() == "/configured_chatter") {
                found = true;
            }
        }
        std::this_thread::sleep_for(50ms);
    }
    EXPECT_TRUE(found);
}
```

Add `test_discovery_snapshot` to `tests/CMakeLists.txt`, linked with `mm_core`.

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target test_discovery_snapshot
```

Expected: build fails because `NodeOptions` and `snapshot_endpoints()` do not exist.

- [ ] **Step 3: Add snapshot API**

In `discovery/include/discovery/discovery_agent.h`, add:

```cpp
struct DiscoveredEndpoint {
    uint64_t participant_id = 0;
    std::string node_name;
    EndpointInfo endpoint;
    Locator locator;
    std::string host_id;
    bool local = false;
};
```

Add public method:

```cpp
std::vector<DiscoveredEndpoint> snapshot_endpoints() const;
```

Because this method is `const`, mark `mtx_` as mutable:

```cpp
mutable std::mutex mtx_;
```

Store remote node names by adding `std::string node_name;` to `Remote`.

In `handle_announcement`, set `r.node_name = ann.node_name();`.

Implement:

```cpp
std::vector<DiscoveredEndpoint> DiscoveryAgent::snapshot_endpoints() const {
    std::vector<DiscoveredEndpoint> out;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& e : local_endpoints_) {
            DiscoveredEndpoint row;
            row.participant_id = participant_id_;
            row.node_name = node_name_;
            row.endpoint = e;
            row.locator = data_locator_;
            row.host_id = local_host_id();
            row.local = true;
            out.push_back(std::move(row));
        }
        for (const auto& kv : remotes_) {
            const Remote& remote = kv.second;
            for (const auto& e : remote.endpoints) {
                DiscoveredEndpoint row;
                row.participant_id = kv.first;
                row.node_name = remote.node_name;
                row.endpoint = e;
                row.locator = remote.locator;
                row.host_id = remote.host_id;
                row.local = false;
                out.push_back(std::move(row));
            }
        }
    }
    return out;
}
```

- [ ] **Step 4: Add NodeOptions**

In `core/include/core/node.h`, add:

```cpp
struct NodeOptions {
    bool enable_shm = true;
    std::string discovery_group = "239.255.0.1";
    uint16_t discovery_port = 7400;
};
```

Add constructor:

```cpp
Node(std::string name, const NodeOptions& options);
```

In `core/src/node.cpp`, make current constructor delegate:

```cpp
Node::Node(std::string name, bool enable_shm)
    : Node(std::move(name), NodeOptions{enable_shm}) {}
```

Move existing constructor body into:

```cpp
Node::Node(std::string name, const NodeOptions& options)
    : name_(std::move(name)), bus_(std::make_shared<LocalBus>()) {
    ...
    discovery_ = std::make_unique<DiscoveryAgent>(
        name_, loc, options.discovery_group, options.discovery_port);
    ...
    data_plane_->set_local_identity(discovery_->participant_id(), local_host_id(), options.enable_shm);
    ...
}
```

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build -j$(nproc) --target test_discovery_snapshot test_node test_node_discovery
cd build && ctest -R "test_discovery_snapshot|test_node|test_node_discovery" --output-on-failure
```

Expected: new snapshot test and existing node tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/include/core/node.h core/src/node.cpp discovery/include/discovery/discovery_agent.h discovery/src/discovery_agent.cpp tests/CMakeLists.txt tests/test_discovery_snapshot.cpp
git commit -m "feat(phase7): expose discovery snapshots and node options"
```

---

## Task 4: CLI Argument Parser

**Files:**
- Create: `cli/CMakeLists.txt`
- Create: `cli/include/cli/args.h`
- Create: `cli/src/args.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/test_cli_args.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing parser tests**

Create `tests/test_cli_args.cpp`:

```cpp
#include "cli/args.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(CliArgs, ParsesTopicList) {
    const char* argv[] = {"mm", "topic", "list", "--wait-ms", "250"};
    auto parsed = parse_cli_args(5, const_cast<char**>(argv));

    ASSERT_EQ(parsed.kind, CliCommandKind::TOPIC_LIST);
    EXPECT_EQ(parsed.wait_ms, 250);
}

TEST(CliArgs, ParsesTopicEcho) {
    const char* argv[] = {"mm", "topic", "echo", "/chatter", "--type", "mm.StringMsg", "--count", "2"};
    auto parsed = parse_cli_args(8, const_cast<char**>(argv));

    ASSERT_EQ(parsed.kind, CliCommandKind::TOPIC_ECHO);
    EXPECT_EQ(parsed.topic, "/chatter");
    EXPECT_EQ(parsed.type_name, "mm.StringMsg");
    EXPECT_EQ(parsed.count, 2);
}

TEST(CliArgs, ParsesTopicHz) {
    const char* argv[] = {"mm", "topic", "hz", "/chatter", "--type", "mm.StringMsg", "--window", "5"};
    auto parsed = parse_cli_args(8, const_cast<char**>(argv));

    ASSERT_EQ(parsed.kind, CliCommandKind::TOPIC_HZ);
    EXPECT_EQ(parsed.topic, "/chatter");
    EXPECT_EQ(parsed.type_name, "mm.StringMsg");
    EXPECT_EQ(parsed.window, 5);
}

TEST(CliArgs, MissingTypeIsUsageError) {
    const char* argv[] = {"mm", "topic", "echo", "/chatter"};
    auto parsed = parse_cli_args(4, const_cast<char**>(argv));

    EXPECT_EQ(parsed.kind, CliCommandKind::ERROR);
    EXPECT_EQ(parsed.exit_code, 2);
    EXPECT_NE(parsed.message.find("--type"), std::string::npos);
}
```

Add `test_cli_args` to `tests/CMakeLists.txt`, linked with `mm_cli_lib`.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target test_cli_args
```

Expected: configure or build fails because `cli/args.h` and `mm_cli_lib` do not exist.

- [ ] **Step 3: Add CLI parser**

Add `add_subdirectory(cli)` in top-level `CMakeLists.txt` after `core`.

Create `cli/include/cli/args.h`:

```cpp
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
```

Create `cli/src/args.cpp` with:

```cpp
#include "cli/args.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace mm {

std::string cli_usage() {
    return "usage: mm topic list [--config FILE] [--wait-ms N]\n"
           "       mm topic echo TOPIC --type TYPE [--config FILE] [--count N]\n"
           "       mm topic hz TOPIC --type TYPE [--config FILE] [--window N] [--count N]\n";
}

namespace {
bool parse_int(const std::string& s, int* out) {
    char* end = nullptr;
    long value = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || value < 0) return false;
    *out = static_cast<int>(value);
    return true;
}

CliCommand error(const std::string& msg) {
    CliCommand cmd;
    cmd.kind = CliCommandKind::ERROR;
    cmd.exit_code = 2;
    cmd.message = msg + "\n" + cli_usage();
    return cmd;
}
}  // namespace

CliCommand parse_cli_args(int argc, char** argv) {
    if (argc <= 1 || std::string(argv[1]) == "--help") {
        CliCommand cmd;
        cmd.kind = CliCommandKind::HELP;
        cmd.message = cli_usage();
        return cmd;
    }
    if (argc < 3 || std::string(argv[1]) != "topic") return error("unknown command");
    if (std::string(argv[2]) == "--help") {
        CliCommand cmd;
        cmd.kind = CliCommandKind::HELP;
        cmd.message = cli_usage();
        return cmd;
    }

    std::string sub = argv[2];
    CliCommand cmd;
    if (sub == "list") cmd.kind = CliCommandKind::TOPIC_LIST;
    else if (sub == "echo") {
        if (argc < 4) return error("topic echo requires TOPIC");
        cmd.kind = CliCommandKind::TOPIC_ECHO;
        cmd.topic = argv[3];
    } else if (sub == "hz") {
        if (argc < 4) return error("topic hz requires TOPIC");
        cmd.kind = CliCommandKind::TOPIC_HZ;
        cmd.topic = argv[3];
    } else {
        return error("unknown topic command");
    }

    int i = (cmd.kind == CliCommandKind::TOPIC_LIST) ? 3 : 4;
    for (; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "--type" || arg == "--wait-ms" ||
             arg == "--count" || arg == "--window") && i + 1 >= argc) {
            return error(arg + " requires a value");
        }
        if (arg == "--config") cmd.config_path = argv[++i];
        else if (arg == "--type") cmd.type_name = argv[++i];
        else if (arg == "--wait-ms") {
            if (!parse_int(argv[++i], &cmd.wait_ms)) return error("--wait-ms must be a non-negative integer");
        } else if (arg == "--count") {
            if (!parse_int(argv[++i], &cmd.count)) return error("--count must be a non-negative integer");
        } else if (arg == "--window") {
            if (!parse_int(argv[++i], &cmd.window) || cmd.window < 2) return error("--window must be >= 2");
        } else {
            return error("unknown option: " + arg);
        }
    }

    if ((cmd.kind == CliCommandKind::TOPIC_ECHO || cmd.kind == CliCommandKind::TOPIC_HZ) &&
        cmd.type_name.empty()) {
        return error("topic echo/hz requires --type");
    }
    return cmd;
}

}  // namespace mm
```

Create `cli/CMakeLists.txt`:

```cmake
add_library(mm_cli_lib STATIC
    src/args.cpp
)

target_include_directories(mm_cli_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mm_cli_lib PUBLIC
    mm_core
    mm_config
)
```

- [ ] **Step 4: Run parser tests**

Run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target test_cli_args
cd build && ctest -R test_cli_args --output-on-failure
```

Expected: `test_cli_args` passes.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt cli tests/CMakeLists.txt tests/test_cli_args.cpp
git commit -m "feat(phase7): parse mm cli arguments"
```

---

## Task 5: Built-In Message Formatting

**Files:**
- Create: `cli/include/cli/message_format.h`
- Create: `cli/src/message_format.cpp`
- Modify: `cli/CMakeLists.txt`
- Create: `tests/test_message_format.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing formatter tests**

Create `tests/test_message_format.cpp`:

```cpp
#include "cli/message_format.h"
#include "messages.pb.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(MessageFormat, FormatsStringMsg) {
    StringMsg msg;
    msg.set_data("hello");
    std::string bytes;
    ASSERT_TRUE(msg.SerializeToString(&bytes));

    auto out = format_message("mm.StringMsg", bytes);

    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_EQ(out.text, "data: hello");
}

TEST(MessageFormat, FormatsPoint3D) {
    Point3D msg;
    msg.set_x(1.0f);
    msg.set_y(2.0f);
    msg.set_z(3.5f);
    std::string bytes;
    ASSERT_TRUE(msg.SerializeToString(&bytes));

    auto out = format_message("mm.Point3D", bytes);

    ASSERT_TRUE(out.ok) << out.error;
    EXPECT_NE(out.text.find("x: 1"), std::string::npos);
    EXPECT_NE(out.text.find("z: 3.5"), std::string::npos);
}

TEST(MessageFormat, RejectsUnsupportedType) {
    auto out = format_message("mm.Unknown", "");
    EXPECT_FALSE(out.ok);
    EXPECT_NE(out.error.find("unsupported"), std::string::npos);
}
```

Add `test_message_format` linked with `mm_cli_lib`.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target test_message_format
```

Expected: build fails because `cli/message_format.h` does not exist.

- [ ] **Step 3: Implement formatter**

Create `cli/include/cli/message_format.h`:

```cpp
#pragma once

#include <string>
#include <vector>

namespace mm {

struct FormatResult {
    bool ok = false;
    std::string text;
    std::string error;
};

FormatResult format_message(const std::string& type_name, const std::string& bytes);
bool is_supported_message_type(const std::string& type_name);
std::vector<std::string> supported_message_types();

}  // namespace mm
```

Create `cli/src/message_format.cpp`:

```cpp
#include "cli/message_format.h"
#include "messages.pb.h"

#include <sstream>

namespace mm {

std::vector<std::string> supported_message_types() {
    return {"mm.StringMsg", "mm.Point3D", "mm.PointCloud"};
}

bool is_supported_message_type(const std::string& type_name) {
    for (const auto& type : supported_message_types()) {
        if (type == type_name) return true;
    }
    return false;
}

FormatResult format_message(const std::string& type_name, const std::string& bytes) {
    std::ostringstream out;
    if (type_name == "mm.StringMsg") {
        StringMsg msg;
        if (!msg.ParseFromString(bytes)) return {false, "", "failed to parse mm.StringMsg"};
        return {true, "data: " + msg.data(), ""};
    }
    if (type_name == "mm.Point3D") {
        Point3D msg;
        if (!msg.ParseFromString(bytes)) return {false, "", "failed to parse mm.Point3D"};
        out << "x: " << msg.x() << "\ny: " << msg.y() << "\nz: " << msg.z();
        return {true, out.str(), ""};
    }
    if (type_name == "mm.PointCloud") {
        PointCloud msg;
        if (!msg.ParseFromString(bytes)) return {false, "", "failed to parse mm.PointCloud"};
        out << "timestamp: " << msg.timestamp()
            << "\nframe_id: " << msg.frame_id()
            << "\npoints: " << msg.points_size();
        return {true, out.str(), ""};
    }
    return {false, "", "unsupported message type: " + type_name};
}

}  // namespace mm
```

Add `src/message_format.cpp` to `mm_cli_lib`.

- [ ] **Step 4: Run formatter tests**

Run:

```bash
cmake --build build -j$(nproc) --target test_message_format
cd build && ctest -R test_message_format --output-on-failure
```

Expected: `test_message_format` passes.

- [ ] **Step 5: Commit**

```bash
git add cli/include/cli/message_format.h cli/src/message_format.cpp cli/CMakeLists.txt tests/CMakeLists.txt tests/test_message_format.cpp
git commit -m "feat(phase7): format built-in message types"
```

---

## Task 6: Topic Command Library

**Files:**
- Create: `cli/include/cli/topic_commands.h`
- Create: `cli/src/topic_commands.cpp`
- Modify: `cli/CMakeLists.txt`
- Create: `tests/test_cli_topic_list.cpp`
- Create: `tests/test_cli_topic_echo.cpp`
- Create: `tests/test_cli_topic_hz.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing topic command tests**

Create `tests/test_cli_topic_list.cpp`:

```cpp
#include "cli/topic_commands.h"
#include "core/node.h"
#include "messages.pb.h"

#include <gtest/gtest.h>
#include <sstream>

using namespace mm;

TEST(CliTopicList, PrintsLocalPublisherFromSnapshot) {
    Node node("topic_list_source");
    auto pub = node.create_publisher<mm::StringMsg>("/cli_list_chatter");

    std::ostringstream out;
    int code = run_topic_list(node.discovery(), out);

    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("/cli_list_chatter"), std::string::npos);
    EXPECT_NE(out.str().find("PUBLISHER"), std::string::npos);
}
```

Create `tests/test_cli_topic_echo.cpp`:

```cpp
#include "cli/topic_commands.h"
#include "core/node.h"
#include "messages.pb.h"

#include <gtest/gtest.h>
#include <chrono>
#include <sstream>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

TEST(CliTopicEcho, PrintsOneStringMessage) {
    Node talker("echo_talker");
    Node listener("echo_cli");
    auto pub = talker.create_publisher<mm::StringMsg>("/cli_echo");

    std::ostringstream out;
    std::thread worker([&] {
        run_topic_echo(listener, "/cli_echo", "mm.StringMsg", 1, out);
    });

    std::this_thread::sleep_for(200ms);
    mm::StringMsg msg;
    msg.set_data("hello");
    for (int i = 0; i < 20; ++i) {
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
    }

    worker.join();
    EXPECT_NE(out.str().find("data: hello"), std::string::npos);
}
```

Create `tests/test_cli_topic_hz.cpp`:

```cpp
#include "cli/topic_commands.h"
#include "core/node.h"
#include "messages.pb.h"

#include <gtest/gtest.h>
#include <chrono>
#include <sstream>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

TEST(CliTopicHz, PrintsRateAfterRepeatedMessages) {
    Node talker("hz_talker");
    Node listener("hz_cli");
    auto pub = talker.create_publisher<mm::StringMsg>("/cli_hz");

    std::ostringstream out;
    std::thread worker([&] {
        run_topic_hz(listener, "/cli_hz", "mm.StringMsg", 5, 4, out);
    });

    std::this_thread::sleep_for(200ms);
    for (int i = 0; i < 20; ++i) {
        mm::StringMsg msg;
        msg.set_data("tick");
        pub->publish(msg);
        std::this_thread::sleep_for(30ms);
    }

    worker.join();
    EXPECT_NE(out.str().find("average rate:"), std::string::npos);
}
```

Add all tests linked with `mm_cli_lib`.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target test_cli_topic_list test_cli_topic_echo test_cli_topic_hz
```

Expected: build fails because `cli/topic_commands.h` does not exist.

- [ ] **Step 3: Implement topic commands**

Create `cli/include/cli/topic_commands.h`:

```cpp
#pragma once

#include "core/node.h"
#include "discovery/discovery_agent.h"

#include <iosfwd>
#include <string>

namespace mm {

int run_topic_list(DiscoveryAgent& discovery, std::ostream& out);
int run_topic_echo(Node& node, const std::string& topic, const std::string& type_name,
                   int count, std::ostream& out);
int run_topic_hz(Node& node, const std::string& topic, const std::string& type_name,
                 int window, int count, std::ostream& out);

}  // namespace mm
```

Create `cli/src/topic_commands.cpp`.

Implementation requirements:
- `run_topic_list()` calls `snapshot_endpoints()`, prints a header, and prints `kind`, topic, type, node name, and locator.
- `run_topic_echo()` supports `mm.StringMsg`, `mm.Point3D`, and `mm.PointCloud` by creating the corresponding typed subscriber. The subscriber serializes the typed message back to bytes, calls `format_message()`, prints text and `---`, increments a protected counter, and notifies a `condition_variable`.
- `run_topic_hz()` uses the same typed subscriber pattern, records `steady_clock::now()` timestamps, and prints rate after `count` messages or once when enough samples are collected.
- If `type_name` is unsupported, print supported types and return `2`.
- If `count <= 0`, block until interrupted by sleeping in a loop. Tests always pass positive count.

Use this helper shape inside `topic_commands.cpp`:

```cpp
template <typename T>
std::shared_ptr<Subscriber<T>> subscribe_typed(
    Node& node, const std::string& topic, std::function<void(const std::string&)> on_bytes) {
    return node.create_subscriber<T>(topic, [on_bytes](const T& msg) {
        std::string bytes;
        if (msg.SerializeToString(&bytes)) on_bytes(bytes);
    });
}
```

- [ ] **Step 4: Run topic command tests**

Run:

```bash
cmake --build build -j$(nproc) --target test_cli_topic_list test_cli_topic_echo test_cli_topic_hz
cd build && ctest -R "test_cli_topic_list|test_cli_topic_echo|test_cli_topic_hz" --output-on-failure
```

Expected: all topic command tests pass.

- [ ] **Step 5: Commit**

```bash
git add cli/include/cli/topic_commands.h cli/src/topic_commands.cpp cli/CMakeLists.txt tests/CMakeLists.txt tests/test_cli_topic_list.cpp tests/test_cli_topic_echo.cpp tests/test_cli_topic_hz.cpp
git commit -m "feat(phase7): implement topic cli commands"
```

---

## Task 7: `mm` Executable Integration

**Files:**
- Create: `cli/src/main.cpp`
- Modify: `cli/CMakeLists.txt`
- Modify: `docs/superpowers/specs/2026-06-15-mini-middleware-roadmap.md`

- [ ] **Step 1: Add executable main**

Create `cli/src/main.cpp`:

```cpp
#include "cli/args.h"
#include "cli/message_format.h"
#include "cli/topic_commands.h"
#include "config/config.h"
#include "core/node.h"

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    mm::CliCommand cmd = mm::parse_cli_args(argc, argv);
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
        auto loaded = mm::load_config_file(cmd.config_path);
        if (!loaded.ok) {
            std::cerr << loaded.error << std::endl;
            return 2;
        }
        cfg = loaded.config;
    }

    mm::NodeOptions opts;
    opts.enable_shm = cfg.transport.enable_shm;
    opts.discovery_group = cfg.discovery.group;
    opts.discovery_port = cfg.discovery.port;
    mm::Node node(cfg.node.name, opts);

    if (cmd.kind == mm::CliCommandKind::TOPIC_LIST) {
        std::this_thread::sleep_for(std::chrono::milliseconds(cmd.wait_ms));
        return mm::run_topic_list(node.discovery(), std::cout);
    }
    if (cmd.kind == mm::CliCommandKind::TOPIC_ECHO) {
        return mm::run_topic_echo(node, cmd.topic, cmd.type_name, cmd.count, std::cout);
    }
    if (cmd.kind == mm::CliCommandKind::TOPIC_HZ) {
        return mm::run_topic_hz(node, cmd.topic, cmd.type_name, cmd.window, cmd.count, std::cout);
    }
    return 2;
}
```

Modify `cli/CMakeLists.txt`:

```cmake
add_executable(mm
    src/main.cpp
)

target_link_libraries(mm PRIVATE
    mm_cli_lib
)
```

- [ ] **Step 2: Build executable**

Run:

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target mm
```

Expected: `build/cli/mm` exists.

- [ ] **Step 3: Smoke-test help**

Run:

```bash
build/cli/mm --help
```

Expected output includes:

```text
mm topic list
mm topic echo
mm topic hz
```

- [ ] **Step 4: Mark roadmap Phase 7 done**

Update `docs/superpowers/specs/2026-06-15-mini-middleware-roadmap.md`:

```markdown
| 7 | CLI 工具 + 配置 | `mm topic list/echo/hz`、YAML 配置加载 | ~900 | 8.7k | ✅ |
```

- [ ] **Step 5: Commit**

```bash
git add cli/src/main.cpp cli/CMakeLists.txt docs/superpowers/specs/2026-06-15-mini-middleware-roadmap.md
git commit -m "feat(phase7): add mm cli executable"
```

---

## Task 8: Full Verification

**Files:**
- No new files.

- [ ] **Step 1: Reconfigure**

```bash
cmake -S . -B build
```

Expected: configure succeeds and includes `config` and `cli`.

- [ ] **Step 2: Build all**

```bash
cmake --build build -j$(nproc)
```

Expected: all libraries, tests, examples, and `mm` build.

- [ ] **Step 3: Run all tests**

```bash
cd build && ctest --output-on-failure
```

Expected: all tests pass, including Phase 7 additions.

- [ ] **Step 4: CLI smoke checks**

Run:

```bash
build/cli/mm --help
build/cli/mm topic list --wait-ms 10
```

Expected: help prints usage; topic list prints a header and exits 0.

- [ ] **Step 5: Inspect git status**

```bash
git status --short
```

Expected: clean working tree after all task commits.
