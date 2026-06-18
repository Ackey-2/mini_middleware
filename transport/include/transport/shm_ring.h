#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// ShmRing:单写多读(SPMC)无锁环形队列,放置在一块裸内存上(可为共享内存)。
//
//   - 不持有内存所有权:只是 base 指针上的一个"视图",故同一块 SHM 段可被
//     写者进程 init()、读者进程 attach() 各自构造一个 ShmRing 来用。
//   - BEST_EFFORT:写者永不阻塞,环满则覆盖最旧槽位;读者靠每槽的序列号
//     检测"被覆盖/撕裂读",丢弃不可用的条目(计入 dropped)。
//   - 每槽按 cache line 对齐,减少写者与读者间的伪共享(false sharing)。
//
// 正确性核心(seqlock 风格):
//   写:store(pos|WRITING) → 写 len/data → store(pos)[release 发布]。
//   读:load(seq)[acquire] 校验 == 期望 pos 且未在写 → 拷贝 → 再 load(seq)
//       校验仍 == pos;不等说明拷贝期间被覆盖(torn read),丢弃。
//
// 依赖:atomic<uint64_t> 必须是 always-lock-free(目标平台 x86-64 成立),
//       否则跨进程共享会退化为带锁,语义不成立。
// ═══════════════════════════════════════════════════════════════

static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "ShmRing requires lock-free 64-bit atomics for cross-process use");

class ShmRingReader;   // 读游标,定义在 ShmRing 之后(需持有完整的 ShmRing)

class ShmRing {
public:
    friend class ShmRingReader;

    static constexpr uint32_t kMagic = 0x4D4D5247;   // "MMRG"
    static constexpr size_t kCacheLine = 64;
    static constexpr uint64_t kWritingBit = 1ULL << 63;   // seq 最高位:槽正在被写

    // 内存布局头部。放在段起始,cache-line 对齐。
    struct Header {
        std::atomic<uint32_t> magic;
        uint32_t slot_count;     // 2 的幂
        uint32_t slot_size;      // 每槽 payload 上限(字节)
        uint32_t _pad0;
        std::atomic<uint64_t> write_seq;   // 已写入条数 = 下一条的 0-based 位置
    };

    // 每槽头部,紧跟其后是 slot_size 字节 payload。
    struct SlotHeader {
        std::atomic<uint64_t> seq;   // 当前占用该槽的条目位置;含 kWritingBit 表示正在写
        uint32_t len;                // payload 实际字节数
        uint32_t _pad0;
    };

    ShmRing() = default;

    // ── 容量计算 ──────────────────────────────────────────────
    static size_t bytes_needed(uint32_t slot_count, uint32_t slot_size) {
        return header_bytes() + static_cast<size_t>(slot_count) * slot_stride(slot_size);
    }

    // ── 构造视图 ──────────────────────────────────────────────
    // 创建者:在 base 上初始化 header 与所有槽(slot_count 必须是 2 的幂)。
    static ShmRing init(void* base, uint32_t slot_count, uint32_t slot_size) {
        ShmRing r;
        r.base_ = static_cast<uint8_t*>(base);
        r.slot_count_ = slot_count;
        r.slot_size_ = slot_size;
        r.stride_ = slot_stride(slot_size);
        Header* h = r.hdr();
        h->slot_count = slot_count;
        h->slot_size = slot_size;
        h->_pad0 = 0;
        h->write_seq.store(0, std::memory_order_relaxed);
        for (uint32_t i = 0; i < slot_count; ++i) {
            SlotHeader* s = r.slot(i);
            // 空槽哨兵:kWritingBit 置位且位置取不可能匹配任何 next_ 的值,
            // 读者据此判定"尚无数据"。
            s->seq.store(kWritingBit, std::memory_order_relaxed);
            s->len = 0;
            s->_pad0 = 0;
        }
        h->magic.store(kMagic, std::memory_order_release);   // 最后发布,reader 见 magic 即就绪
        return r;
    }

    // 读者:附着到已初始化的 base;magic 不符返回 invalid 视图。
    static ShmRing attach(void* base) {
        ShmRing r;
        Header* h = static_cast<Header*>(base);
        if (h->magic.load(std::memory_order_acquire) != kMagic) return r;   // invalid
        r.base_ = static_cast<uint8_t*>(base);
        r.slot_count_ = h->slot_count;
        r.slot_size_ = h->slot_size;
        r.stride_ = slot_stride(h->slot_size);
        return r;
    }

    bool valid() const { return base_ != nullptr; }
    uint32_t slot_count() const { return slot_count_; }
    uint32_t slot_capacity() const { return slot_size_; }
    uint64_t write_seq() const {
        return hdr()->write_seq.load(std::memory_order_acquire);
    }

    // ── 写 ────────────────────────────────────────────────────
    // 写入一条消息。len>容量返回 false(调用方告警丢弃)。永不阻塞。
    bool write(const void* data, uint32_t len) {
        if (len > slot_size_) return false;
        Header* h = hdr();
        uint64_t pos = h->write_seq.fetch_add(1, std::memory_order_acq_rel);
        SlotHeader* s = slot(static_cast<uint32_t>(pos) & (slot_count_ - 1));
        s->seq.store(pos | kWritingBit, std::memory_order_release);   // 占用,旧数据立即失效
        s->len = len;
        std::memcpy(slot_data(s), data, len);
        s->seq.store(pos, std::memory_order_release);                 // 发布就绪
        return true;
    }
    bool write(const std::string& bytes) {
        return write(bytes.data(), static_cast<uint32_t>(bytes.size()));
    }

    // 读游标(定义于本类之后)。
    ShmRingReader make_reader() const;

private:
    static size_t align_up(size_t n, size_t a) { return (n + a - 1) & ~(a - 1); }
    static size_t header_bytes() { return align_up(sizeof(Header), kCacheLine); }
    static size_t slot_stride(uint32_t slot_size) {
        return align_up(sizeof(SlotHeader) + slot_size, kCacheLine);
    }

    Header* hdr() const { return reinterpret_cast<Header*>(base_); }
    SlotHeader* slot(uint32_t i) const {
        return reinterpret_cast<SlotHeader*>(base_ + header_bytes() + static_cast<size_t>(i) * stride_);
    }
    static char* slot_data(SlotHeader* s) {
        return reinterpret_cast<char*>(s) + sizeof(SlotHeader);
    }

    uint8_t* base_ = nullptr;
    uint32_t slot_count_ = 0;
    uint32_t slot_size_ = 0;
    size_t stride_ = 0;
};

// ═══════════════════════════════════════════════════════════════
// ShmRingReader:某个读者的私有游标(next_/dropped_)。多个 reader 各持一个,
// 互不影响,故支持单写多读(SPMC)。创建时 next_ 取当前 write_seq,即只看未来。
// ═══════════════════════════════════════════════════════════════
class ShmRingReader {
public:
    ShmRingReader() = default;
    explicit ShmRingReader(ShmRing ring)
        : ring_(ring), next_(ring.valid() ? ring.write_seq() : 0) {}

    // 读下一条;成功写入 out 返回 true,无新数据返回 false。
    // 落后超过环容量则快进到最新可用窗口,丢失条目计入 dropped()。
    bool read(std::string& out) {
        if (!ring_.valid()) return false;
        const uint32_t n = ring_.slot_count_;
        for (;;) {
            uint64_t w = ring_.write_seq();
            if (next_ >= w) return false;                  // 无新数据
            if (w - next_ > n) {                           // 落后太多 → 快进
                uint64_t target = w - n + 1;               // 留 1 槽余量,避开正被写的最旧槽
                dropped_ += target - next_;
                next_ = target;
            }
            ShmRing::SlotHeader* s = ring_.slot(static_cast<uint32_t>(next_) & (n - 1));
            uint64_t s1 = s->seq.load(std::memory_order_acquire);
            uint64_t pos1 = s1 & ~ShmRing::kWritingBit;
            if (pos1 != next_) {                           // 槽已被更新的数据覆盖
                dropped_++;
                next_++;
                continue;
            }
            if (s1 & ShmRing::kWritingBit) return false;   // 正好在写这一条 → 稍后再来
            uint32_t len = s->len;
            if (len > ring_.slot_size_) {                  // 防御:异常长度,跳过
                dropped_++;
                next_++;
                continue;
            }
            out.assign(ShmRing::slot_data(s), len);
            uint64_t s2 = s->seq.load(std::memory_order_acquire);
            if (s2 != next_) {                             // 拷贝期间被覆盖(撕裂读)
                dropped_++;
                next_++;
                continue;
            }
            next_++;
            return true;
        }
    }

    uint64_t dropped() const { return dropped_; }

private:
    ShmRing ring_;
    uint64_t next_ = 0;     // 下一个要读的 0-based 位置
    uint64_t dropped_ = 0;
};

inline ShmRingReader ShmRing::make_reader() const { return ShmRingReader(*this); }

}  // namespace mm
