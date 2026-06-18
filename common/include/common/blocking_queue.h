#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <chrono>

namespace mm {

template <typename T>
class BlockingQueue {
public:
    // capacity=0 → 无界(KEEP_ALL);capacity>0 → 上限,满时丢最旧(KEEP_LAST N)。
    explicit BlockingQueue(size_t capacity = 0) : capacity_(capacity) {}
    ~BlockingQueue() = default;

    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;


    bool push(T item) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (closed_) return false;
            // KEEP_LAST:容量已满则丢弃最旧一条,保证只留最近 capacity_ 条。
            if (capacity_ > 0 && queue_.size() >= capacity_) {
                queue_.pop();
                ++dropped_;
            }
            queue_.push(std::move(item));
        }
        cv_.notify_one();
        return true;
    }


    bool pop(T& out) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock,[this]{return !queue_.empty()||closed_;});
        if (queue_.empty()) {
        return false;
        }
        out=std::move(queue_.front());
        queue_.pop();
        return true;
    }


    bool try_pop(T& out, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mtx_);
        bool success=cv_.wait_for(lock,timeout,[this]{ return !queue_.empty()||closed_ ;});
        if(!success){
            return false;
        }
        if (queue_.empty()) return false;
        out=std::move(queue_.front());
        queue_.pop();
        return true;
    }


 
    void close() {
        {
        std::lock_guard<std::mutex> lock(mtx_);
        closed_=true;
        }
        cv_.notify_all();
    }


    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

    // KEEP_LAST 下因满而丢弃的累计条数(BEST_EFFORT 监控用)。
    uint64_t dropped() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return dropped_;
    }

private:
    mutable std::mutex mtx_;       // mutable:const 函数(size)里也能 lock
    std::condition_variable cv_;
    std::queue<T> queue_;
    size_t capacity_ = 0;          // 0=无界
    uint64_t dropped_ = 0;
    bool closed_ = false;
};

}  // namespace mm