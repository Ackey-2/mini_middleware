#include "transport/shm_segment.h"
#include "common/logger.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace mm {

ShmSegment::~ShmSegment() {
    if (addr_ && addr_ != MAP_FAILED) ::munmap(addr_, size_);
    if (fd_ >= 0) ::close(fd_);
    if (owner_ && !name_.empty()) ::shm_unlink(name_.c_str());   // 仅创建者归还
}

std::unique_ptr<ShmSegment> ShmSegment::create(const std::string& name,
                                               uint32_t slot_count, uint32_t slot_size) {
    size_t bytes = ShmRing::bytes_needed(slot_count, slot_size);

    // O_CREAT(非 EXCL):容忍上次进程被 kill 残留的同名段,覆盖式重用并重新初始化。
    int fd = ::shm_open(name.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        LOG_ERROR("shm create: shm_open({}) failed: {}", name, std::strerror(errno));
        return nullptr;
    }
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        LOG_ERROR("shm create: ftruncate({}) failed: {}", name, std::strerror(errno));
        ::close(fd);
        ::shm_unlink(name.c_str());
        return nullptr;
    }
    void* addr = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        LOG_ERROR("shm create: mmap({}) failed: {}", name, std::strerror(errno));
        ::close(fd);
        ::shm_unlink(name.c_str());
        return nullptr;
    }

    std::unique_ptr<ShmSegment> seg(new ShmSegment());
    seg->name_ = name;
    seg->fd_ = fd;
    seg->addr_ = addr;
    seg->size_ = bytes;
    seg->owner_ = true;
    seg->ring_ = ShmRing::init(addr, slot_count, slot_size);
    return seg;
}

std::unique_ptr<ShmSegment> ShmSegment::open(const std::string& name) {
    int fd = ::shm_open(name.c_str(), O_RDWR, 0600);
    if (fd < 0) return nullptr;   // 段尚不存在:静默,由调用方轮询重试

    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(ShmRing::Header))) {
        ::close(fd);
        return nullptr;   // 写者刚 shm_open 但还没 ftruncate,稍后再试
    }
    size_t bytes = static_cast<size_t>(st.st_size);
    void* addr = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        ::close(fd);
        return nullptr;
    }
    ShmRing ring = ShmRing::attach(addr);
    if (!ring.valid()) {          // magic 未就绪:写者还没 init 完
        ::munmap(addr, bytes);
        ::close(fd);
        return nullptr;
    }

    std::unique_ptr<ShmSegment> seg(new ShmSegment());
    seg->name_ = name;
    seg->fd_ = fd;
    seg->addr_ = addr;
    seg->size_ = bytes;
    seg->owner_ = false;
    seg->ring_ = ring;
    return seg;
}

}  // namespace mm
