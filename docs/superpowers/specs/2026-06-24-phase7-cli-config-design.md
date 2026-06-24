# Phase 7 Design: CLI Tools and YAML Configuration

> Roadmap: `2026-06-15-mini-middleware-roadmap.md`
> Prerequisites: Phase 1-6 are complete on `main`: Pub/Sub, discovery, TCP, SHM, QoS, and Service/RPC.
> Goal: add a usable `mm` CLI plus shared configuration loading, so the project can be operated and demonstrated like a small DDS/ROS-style middleware.

## 1. Goals and Non-Goals

**Goals**
- Build one CLI executable named `mm`.
- Support `mm topic list`, `mm topic echo`, and `mm topic hz`.
- Load runtime settings from a YAML config file.
- Keep YAML parsing outside `core` so the middleware library stays dependency-light.
- Support built-in protobuf message types: `mm.StringMsg`, `mm.Point3D`, and `mm.PointCloud`.
- Add tests for config parsing, CLI argument parsing, topic formatting, and topic command behavior.

**Non-goals**
- Fully dynamic protobuf descriptor loading from arbitrary `.proto` files.
- A shell-completion system or polished package installation.
- CLI support for service calls, bag recording, graph visualization, or benchmark commands.
- Making every example use YAML immediately. Examples may keep simple argv flags unless the config loader naturally improves them.

## 2. Command Surface

```bash
mm topic list [--config config.yaml] [--wait-ms 1500]
mm topic echo /topic --type mm.StringMsg [--config config.yaml] [--count N]
mm topic hz /topic --type mm.StringMsg [--config config.yaml] [--window N] [--count N]
mm --help
mm topic --help
```

Behavior:
- `topic list` starts a temporary CLI node, listens for discovery announcements for `wait-ms`, and prints known topics and endpoints.
- `topic echo` subscribes to one topic and prints each received message in a readable text form.
- `topic hz` subscribes to one topic and prints message frequency based on inter-arrival timestamps.
- `--type` is required for `echo` and `hz` because Phase 7 supports a fixed built-in type registry, not dynamic protobuf descriptors.
- `--count N` exits after receiving `N` messages or samples. Without `--count`, the command runs until interrupted.

## 3. YAML Config

Supported YAML subset:

```yaml
node:
  name: mm_cli
transport:
  enable_shm: true
discovery:
  group: 239.255.0.1
  port: 7400
qos:
  reliability: reliable   # best_effort | reliable
  history: keep_last      # keep_last | keep_all
  depth: 16
```

Defaults:
- `node.name = "mm_cli"`
- `transport.enable_shm = true`
- `discovery.group = "239.255.0.1"`
- `discovery.port = 7400`
- `qos.reliability = best_effort`
- `qos.history = keep_last`
- `qos.depth = 16`

Parsing strategy:
- Prefer `yaml-cpp` when available through CMake.
- If `yaml-cpp` is not installed, use a small parser for this exact nested key/value subset.
- The fallback parser supports comments, blank lines, two-space indentation, booleans, unsigned integers, and strings.
- Invalid enum values or malformed integers return a parse error with a human-readable message.

`core` does not include YAML headers. The config module converts parsed settings into `Node` constructor arguments and `Qos`.

## 4. Modules and Responsibilities

| Component | Responsibility |
|---|---|
| `config/include/config/config.h` | `MiddlewareConfig`, default values, and parse result types. |
| `config/src/config.cpp` | Load config from file and parse YAML subset or `yaml-cpp` nodes. |
| `cli/include/cli/args.h` | Small command model for parsed CLI arguments. |
| `cli/src/args.cpp` | Parse argv into `TopicList`, `TopicEcho`, `TopicHz`, or help/error. |
| `cli/src/message_format.cpp` | Convert built-in protobuf messages to readable text. |
| `cli/src/topic_commands.cpp` | Implement `topic list`, `topic echo`, and `topic hz`. |
| `cli/src/main.cpp` | Wire args, config, and command execution into the `mm` executable. |
| `discovery/DiscoveryAgent` | Expose a read-only snapshot of local and remote endpoints for `topic list`. |
| `core/Node` | Allow discovery group/port configuration while preserving current constructor defaults. |

## 5. Discovery Snapshot

`topic list` needs a safe way to inspect discovery state. Add a read-only snapshot API to `DiscoveryAgent`:

```cpp
struct DiscoveredEndpoint {
    uint64_t participant_id;
    std::string node_name;
    EndpointInfo endpoint;
    Locator locator;
    std::string host_id;
    bool local;
};

std::vector<DiscoveredEndpoint> snapshot_endpoints() const;
```

The method copies data under existing mutexes and returns value objects. It does not expose internal maps or callbacks.

`Node` keeps the existing constructor:

```cpp
explicit Node(std::string name, bool enable_shm = true);
```

and gains a `NodeOptions` overload:

```cpp
struct NodeOptions {
    bool enable_shm = true;
    std::string discovery_group = "239.255.0.1";
    uint16_t discovery_port = 7400;
};

Node(std::string name, const NodeOptions& options);
```

The current constructor delegates to the options constructor to preserve existing behavior.

## 6. Message Type Registry

Phase 7 uses a small built-in registry:

| Type name | CLI support |
|---|---|
| `mm.StringMsg` | echo/hz parse and print `data`. |
| `mm.Point3D` | echo/hz parse and print `x y z`. |
| `mm.PointCloud` | echo/hz parse and print timestamp, frame id, and point count. |

The registry maps type names to:
- protobuf descriptor name;
- subscriber creation function;
- formatter function.

This keeps Phase 7 focused and avoids dynamic protobuf descriptor complexity. The design leaves room for a later dynamic registry.

## 7. Command Details

### `mm topic list`

Output columns:

```text
KIND        TOPIC                 TYPE             NODE          LOCATOR
PUBLISHER   /chatter              mm.StringMsg     talker        127.0.0.1:39123
SUBSCRIBER  /chatter              mm.StringMsg     listener      127.0.0.1:40711
SERVICE     /echo                 mm.StringMsg -> mm.StringMsg   rpc_server 127.0.0.1:41002
```

It includes local and remote endpoints. It de-duplicates exact duplicate rows from repeated announcements.

### `mm topic echo`

For `mm.StringMsg`:

```text
data: hello
---
data: world
---
```

For unknown `--type`, exit non-zero and list supported types.

### `mm topic hz`

The command records receive timestamps and prints frequency once at least two messages are available:

```text
average rate: 9.98 Hz
min: 0.099s max: 0.101s window: 10
```

`--window` controls the number of intervals used in the moving average. Default is 10.

## 8. Error Handling

- Missing command or `--help`: print usage and exit 0.
- Unknown command or missing required argument: print usage and exit 2.
- Config file missing or invalid: print parse error and exit 2.
- Unsupported message type: print supported type list and exit 2.
- Subscriber parse failure: print a warning and continue.
- `topic list` with no endpoints: print only the header and exit 0 after the wait period.

## 9. Testing

- `test_config_defaults`: default config values.
- `test_config_parse`: YAML overrides for node, transport, discovery, and QoS.
- `test_config_errors`: invalid reliability/history/port/depth produce parse errors.
- `test_cli_args`: parse topic list/echo/hz and common error cases.
- `test_message_format`: format built-in message types.
- `test_discovery_snapshot`: snapshot includes local and remote endpoints without exposing mutable state.
- `test_cli_topic_list`: start a publisher node and verify the list command captures its topic.
- `test_cli_topic_echo`: publish `StringMsg` and verify echo output with `--count 1`.
- `test_cli_topic_hz`: publish repeated `StringMsg` messages and verify rate output appears.
- Full suite remains green.

## 10. Scope Boundary

Phase 7 should make the middleware operable from a terminal without turning into a full ROS tooling project. The core deliverable is a reliable CLI and a reusable config loader. Dynamic protobuf loading, service CLI commands, and benchmark-grade output belong in later phases.
