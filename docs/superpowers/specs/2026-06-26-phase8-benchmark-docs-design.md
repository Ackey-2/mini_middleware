# Phase 8 Design: Benchmark and Demo Documentation

> Roadmap: `2026-06-15-mini-middleware-roadmap.md`
> Prerequisites: Phase 1-7 are complete on `main`: Pub/Sub, discovery, TCP, SHM, QoS, Service/RPC, CLI, and YAML configuration.
> Goal: turn the middleware into a strong campus-recruiting demo by adding a reproducible benchmark entry point and rewriting the README around architecture, verification, and interview talking points.

## 1. Goals and Non-Goals

**Goals**
- Add a benchmark executable that compares same-host TCP and SHM data paths.
- Report human-readable latency and throughput metrics: count, payload bytes, total time, throughput, average latency, p50, p95, and p99.
- Keep benchmark runtime short enough for local demonstration and automated smoke tests.
- Add focused tests for benchmark statistics and argument parsing.
- Rewrite `README.md` so a reviewer can build, run, verify, and understand the project quickly.
- Add simple architecture diagrams in Markdown using Mermaid or ASCII, without adding image-generation or rendering dependencies.
- Mark Phase 8 complete in the roadmap after implementation.

**Non-goals**
- A full performance lab with CPU pinning, warm-up calibration, CSV export, dashboards, or long-duration stress testing.
- Cross-machine benchmark orchestration.
- CI setup, packaging, installers, or release artifacts.
- Dynamic benchmark message schemas beyond the built-in protobuf messages.
- Reworking core transport internals unless a benchmark test exposes a correctness issue.

## 2. Benchmark Command Surface

Create one executable named `mm_bench` under the build tree:

```bash
build/bench/mm_bench --mode shm --count 10000 --payload-bytes 256
build/bench/mm_bench --mode tcp --count 10000 --payload-bytes 256
```

Supported options:

| Option | Default | Meaning |
|---|---:|---|
| `--mode shm|tcp` | `shm` | `shm` enables SHM on the node; `tcp` disables SHM to force the same-host TCP path. |
| `--count N` | `10000` | Number of messages to publish and wait for. |
| `--payload-bytes N` | `256` | Size of the string payload in each message. |
| `--topic NAME` | `/bench` | Topic used by the benchmark. |
| `--help` | - | Print usage and exit zero. |

The command creates a publisher node and a subscriber node in the same process. This keeps the demo easy to run while still exercising the normal `Node`, discovery, data-plane routing, publisher, and subscriber APIs. `--mode tcp` passes `NodeOptions{.enable_shm = false}` to force the TCP route. `--mode shm` uses the default SHM-enabled route.

## 3. Measured Metrics

Each message carries a send timestamp and a string payload. The subscriber records receive timestamps and stores one latency sample per received message.

Output format:

```text
mode: shm
messages: 10000
payload_bytes: 256
received: 10000
duration_ms: 123.45
throughput_msg_s: 81004.45
latency_us_avg: 18.20
latency_us_p50: 16.00
latency_us_p95: 29.00
latency_us_p99: 44.00
```

Latency is measured with `std::chrono::steady_clock` in microseconds. Percentiles use sorted samples and nearest-rank indexing. If `received < count`, the command still prints the received count and returns non-zero after a bounded timeout.

The numbers are demo-grade, not publication-grade. The README will state that results depend on machine load, build type, and WSL/host environment.

## 4. Modules and Responsibilities

| Component | Responsibility |
|---|---|
| `bench/CMakeLists.txt` | Build `mm_bench` and `mm_bench_lib`; link against `mm_core` and `mm_proto`. |
| `bench/include/bench/bench_args.h` | Define `BenchOptions`, `BenchMode`, parse result, and usage text. |
| `bench/src/bench_args.cpp` | Parse and validate command-line options. |
| `bench/include/bench/stats.h` | Define `LatencyStats` and calculation APIs. |
| `bench/src/stats.cpp` | Compute average, percentiles, duration, and throughput. |
| `bench/src/main.cpp` | Run the publisher/subscriber benchmark and print metrics. |
| `tests/test_bench_args.cpp` | Cover valid options, defaults, help, and invalid values. |
| `tests/test_bench_stats.cpp` | Cover average, percentile, throughput, empty samples, and single-sample cases. |
| `README.md` | Replace early scaffold text with current architecture, features, build, CLI, benchmark, and interview notes. |
| `docs/superpowers/specs/2026-06-15-mini-middleware-roadmap.md` | Mark Phase 8 complete after implementation. |

## 5. Benchmark Data Flow

```mermaid
flowchart LR
    A["Publisher Node"] --> B["Publisher<StringMsg>"]
    B --> C["DataPlane route"]
    C --> D{"Mode"}
    D -->|"shm"| E["Shared-memory path"]
    D -->|"tcp"| F["TCP loopback path"]
    E --> G["Subscriber<StringMsg>"]
    F --> G
    G --> H["Latency samples"]
    H --> I["Stats report"]
```

The benchmark should wait briefly after creating endpoints so discovery and route setup can settle. It should then publish messages as fast as the API allows, while the subscriber records arrival time. A condition variable waits until all messages arrive or a timeout expires.

Payload format:

```text
<sequence>|<send_steady_clock_nanoseconds>|<padding>
```

The parser only needs to extract the sequence and timestamp prefix. Padding is deterministic and sized to reach `payload_bytes`.

## 6. Error Handling

- Unknown option: print usage and exit `2`.
- Invalid mode: print supported modes and exit `2`.
- Non-positive `--count`: print an error and exit `2`.
- `--payload-bytes` too small to contain metadata: automatically grow payload to the minimum required size and report the effective size.
- Timeout before all messages arrive: print partial metrics and exit `1`.
- Serialization or publish failure: print the failing sequence and exit `1`.

## 7. README Shape

The README should become the main project landing page:

1. Project one-liner: "DDS-style lightweight robot middleware in C++17."
2. Architecture diagram: discovery plane, data plane, SHM/TCP routing, Node/Pub/Sub/RPC.
3. Feature checklist with completed phases.
4. Build and full verification commands.
5. CLI examples for `mm topic list`, `mm topic echo`, and YAML config.
6. Benchmark examples for SHM and TCP.
7. Demo workflow for a reviewer or interviewer.
8. Interview talking points: epoll, UDP multicast discovery, SHM ring buffer, QoS negotiation, RPC, CMake modularity.
9. Known limitations: demo-grade benchmark, no dynamic protobuf loading, no production-grade security.

The README should use plain Markdown and Mermaid only. It should not depend on external images.

## 8. Testing and Verification

Automated tests:
- `test_bench_args`: default parse, explicit TCP/SHM modes, count and payload parsing, help, missing values, invalid numeric values.
- `test_bench_stats`: empty sample behavior, single sample, multiple samples, percentile ordering, throughput calculation.
- Existing full suite remains green.

Manual verification:

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
../build/bench/mm_bench --mode shm --count 1000 --payload-bytes 256
../build/bench/mm_bench --mode tcp --count 1000 --payload-bytes 256
```

The benchmark commands should print all metrics and exit zero on a normal local run.

## 9. Scope Boundary

Phase 8 is the project presentation phase, not a new transport phase. The implementation should favor a clear, repeatable, easy-to-explain demo over maximum benchmark sophistication. If deeper performance work is needed later, it should become a separate phase with CSV output, pinned processes, cross-machine runs, and more rigorous methodology.
