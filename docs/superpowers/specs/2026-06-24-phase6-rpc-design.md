# Phase 6 Design: Service/RPC

> Roadmap: `2026-06-15-mini-middleware-roadmap.md`
> Prerequisites: Phase 1-5 are complete on `main`: Node/Pub/Sub, UDP discovery, TCP data plane, SHM, QoS.
> Goal: add DDS-style service/client discovery and request-response APIs without building a second transport stack.

## 1. Goals and Non-Goals

**Goals**
- Add `Node::create_service<Req, Resp>()` and `Node::create_client<Req, Resp>()`.
- Extend discovery endpoint kinds with `SERVICE` and `CLIENT`.
- Match RPC endpoints by service name, request type, and response type.
- Implement request-response on top of the existing Pub/Sub data plane.
- Support synchronous `Client::call(request, timeout)` with request ids and timeout cleanup.
- Add unit and end-to-end tests plus an `rpc_demo`.

**Non-goals**
- Streaming RPC, cancellation, retries, load balancing, or server-side async completion.
- A new TCP frame type or separate RPC transport.
- IDL/code generation for service definitions.
- Strong service readiness guarantees before the first request. Discovery is eventually consistent; callers should still use a timeout.

## 2. Key Decisions

1. **RPC discovery is first-class.** `EndpointInfo::Kind` gains `SERVICE` and `CLIENT`, so the project can demonstrate service discovery rather than hiding RPC behind ordinary pub/sub endpoints.
2. **RPC data uses internal Pub/Sub topics.** A service named `/echo` owns request topic `/_mm/rpc/echo/request`. Each client owns a unique reply topic such as `/_mm/rpc/echo/reply/<client_id>`.
3. **Request and response type names are explicit in discovery.** `EndpointInfo.type_name` stores request type for all endpoint kinds. A new `response_type_name` field stores response type for `SERVICE` and `CLIENT`.
4. **The service handler is synchronous.** The first version runs the handler in the subscriber callback path and publishes one response. This keeps the API small and the behavior easy to test.
5. **`Client::call()` returns `std::optional<Resp>`.** It returns empty on timeout, serialization failure, parse failure, or no response.
6. **QoS defaults remain compatible.** RPC internals use default QoS unless callers pass a `Qos`. Reliability negotiation for RPC endpoints uses the existing reliability field.

## 3. Public API

```cpp
auto service = node.create_service<mm::StringMsg, mm::StringMsg>(
    "/echo",
    [](const mm::StringMsg& req) {
        mm::StringMsg resp;
        resp.set_data("echo: " + req.data());
        return resp;
    });

auto client = node.create_client<mm::StringMsg, mm::StringMsg>("/echo");

mm::StringMsg req;
req.set_data("hello");
auto resp = client->call(req, std::chrono::milliseconds(1000));
```

`Service<Req, Resp>` owns:
- a request subscriber on the internal request topic;
- a response publisher per incoming reply topic, created lazily and reused.

`Client<Req, Resp>` owns:
- a request publisher on the service request topic;
- a reply subscriber on its private reply topic;
- a pending map keyed by `request_id`.

## 4. Wire Messages

Add `proto/rpc.proto`:

```proto
syntax = "proto3";

package mm;

message RpcRequest {
    uint64 request_id = 1;
    string reply_topic = 2;
    bytes payload = 3;
}

message RpcReply {
    uint64 request_id = 1;
    bytes payload = 2;
    bool ok = 3;
    string error = 4;
}
```

`payload` contains the serialized user protobuf request or response.

## 5. Discovery Matching

`EndpointInfo` changes:

```proto
enum Kind {
    PUBLISHER = 0;
    SUBSCRIBER = 1;
    SERVICE = 2;
    CLIENT = 3;
}

string response_type_name = 5;
```

Matching rules:
- Pub/Sub: unchanged. One side must be `PUBLISHER`, the other `SUBSCRIBER`, and `topic + type_name` must match.
- RPC: one side must be `SERVICE`, the other `CLIENT`, and `topic + type_name + response_type_name` must match.
- Reliability RxO is applied to both Pub/Sub and RPC pairs.
- Other kind combinations do not match.

## 6. Components

| Component | Responsibility |
|---|---|
| `proto/rpc.proto` | RPC request/reply envelopes. |
| `proto/discovery.proto` | Add `SERVICE`, `CLIENT`, and `response_type_name`. |
| `discovery/endpoint_matcher.*` | Support Pub/Sub and RPC matching with shared QoS compatibility checks. |
| `discovery/discovery_agent.*` | Add `response_type_name` argument to `add_endpoint`, defaulting to empty for Pub/Sub. |
| `core/rpc_topics.h` | Sanitize service names and derive internal request/reply topics. |
| `core/service.h` | Template service wrapper around request subscriber and response publishers. |
| `core/client.h` | Template synchronous client with pending request map and timeout. |
| `core/node.h` | Add `create_service` and `create_client`, register RPC discovery endpoints. |
| `examples/rpc_demo.cpp` | Two-process echo service demo. |

## 7. Error Handling

- Request parse failure: log and do not invoke the handler.
- Handler exception: publish an `RpcReply` with `ok=false` and the exception message.
- Response parse failure: complete the pending call with empty optional.
- Timeout: remove the pending entry and return empty optional.
- Late response after timeout: ignore it.
- Empty or malformed service name: normalize it into a valid internal topic segment; the public service name in discovery remains unchanged.

## 8. Verification

- `test_rpc_proto`: request/reply envelope round trip.
- `test_rpc_topics`: service names map to stable request/reply topics.
- `test_endpoint_matcher`: Pub/Sub matching stays unchanged; RPC kind/type matches and mismatches are covered.
- `test_rpc_local`: one `Node` can host service and client and complete a call.
- `test_rpc_timeout`: client without service returns empty optional after timeout.
- `test_rpc_pubsub`: two `Node` instances complete an RPC call through discovery and data plane.
- Full suite: `cmake --build build -j$(nproc)` and `cd build && ctest --output-on-failure`.

## 9. Scope Boundary

This phase should stay small enough to be reviewed in one pass. The implementation favors explicit templates and existing Pub/Sub plumbing over generalized RPC frameworks. Advanced service behavior belongs in a later phase.
