#pragma once

#include "transport/shm_ring.h"

#include <cstdint>
#include <memory>
#include <string>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// ShmSegment:一段 POSIX 共享内存(/dev/shm/<name>)的 RAII 句柄,
// 内部承载一个 ShmRing。
//   - create():写者侧。shm_open(O_CREAT) + ftruncate + mmap,init 出 ring。
//     析构时 shm_unlink(归还内核,/dev/shm 下的名字消失)。
//   - open():读者侧。shm_open(只读打开) + mmap,attach 到已有 ring。
//     段不存在(写者尚未创建)返回 nullptr,调用方可轮询重试。析构不 unlink。
//
// name 形如 "/mm.<pid>.<topic>"(POSIX 要求以 '/' 开头且不再含 '/')。
// ═══════════════════════════════════════════════════════════════
class ShmSegment {
public:
    ~ShmSegment();

    ShmSegment(const ShmSegment&) = delete;
    ShmSegment& operator=(const ShmSegment&) = delete;

    // 写者:创建/覆盖式打开并初始化。失败返回 nullptr。
    static std::unique_ptr<ShmSegment> create(const std::string& name,
                                              uint32_t slot_count, uint32_t slot_size);

    // 读者:附着到已存在的段。不存在或未初始化返回 nullptr。
    static std::unique_ptr<ShmSegment> open(const std::string& name);

    ShmRing& ring() { return ring_; }
    const std::string& name() const { return name_; }

private:
    ShmSegment() = default;

    std::string name_;
    int fd_ = -1;
    void* addr_ = nullptr;
    size_t size_ = 0;
    bool owner_ = false;   // true=create 出来的,析构时 unlink
    ShmRing ring_;
};

}  // namespace mm
