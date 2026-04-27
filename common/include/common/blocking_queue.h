#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <chrono>

namespace mm {

template <typename T>
class BlockingQueue {
public: 
    BlockingQueue() = default;
    ~BlockingQueue() = default;

    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;


    bool push(T item) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (closed_) return false;
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

private:
    mutable std::mutex mtx_;       // mutable:const 函数(size)里也能 lock
    std::condition_variable cv_;
    std::queue<T> queue_;
    bool closed_ = false;
};

}  // namespace mm