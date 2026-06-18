#pragma once

#include <cstdint>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Qos:服务质量策略(DDS 风格,精简版)。
//   - reliability:BEST_EFFORT(尽力,可丢) | RELIABLE(可靠,经 TCP 保证有序不丢)。
//   - history:KEEP_LAST(只保留最近 depth 条) | KEEP_ALL(不限)。
//   - depth:KEEP_LAST 的 N,同时是订阅队列上限。
//
// 协商(发现期):只有 reliability 参与 RxO(Requested ≤ Offered)兼容判定;
// history/depth 是本地策略,不上线、不协商(与 DDS 一致)。
// ═══════════════════════════════════════════════════════════════
struct Qos {
    enum class Reliability { BEST_EFFORT = 0, RELIABLE = 1 };
    enum class History { KEEP_LAST = 0, KEEP_ALL = 1 };

    Reliability reliability = Reliability::BEST_EFFORT;
    History history = History::KEEP_LAST;
    uint32_t depth = 16;   // KEEP_LAST 深度 = 订阅队列上限

    // RxO 兼容:writer 提供的可靠性 ≥ reader 请求的才兼容。
    //   reader BEST_EFFORT → 兼容任何 writer。
    //   reader RELIABLE    → 只兼容 writer RELIABLE。
    static bool compatible(Reliability offered_writer, Reliability requested_reader) {
        return static_cast<int>(offered_writer) >= static_cast<int>(requested_reader);
    }
};

}  // namespace mm
