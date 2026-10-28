#include "WorkerPool.hpp"

#include <algorithm>

namespace bibo::world {

WorkerPool::WorkerPool(unsigned int thread_count) {
  if (thread_count == 0) {
    unsigned int processors = std::thread::hardware_concurrency();
    thread_count = std::clamp(processors > 1 ? processors - 1 : 1u, 1u, 6u);
  }
  for (unsigned int i = 0; i < thread_count; ++i) {
    threads_.emplace_back([this] { Run(); });
  }
}

WorkerPool::~WorkerPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    queue_.clear();
  }
  wake_.notify_all();
  for (std::thread& thread : threads_) {
    thread.join();
  }
}

void WorkerPool::Submit(std::function<void()> job, float priority) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back({priority, submitted_++, std::move(job)});
    std::push_heap(queue_.begin(), queue_.end());
  }
  wake_.notify_one();
}

void WorkerPool::CancelQueued() {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
  if (running_ == 0) {
    idle_.notify_all();
  }
}

void WorkerPool::WaitIdle() {
  std::unique_lock<std::mutex> lock(mutex_);
  idle_.wait(lock, [this] { return queue_.empty() && running_ == 0; });
}

std::size_t WorkerPool::GetQueuedCount() {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

void WorkerPool::Run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
    if (stopping_) {
      return;
    }
    std::pop_heap(queue_.begin(), queue_.end());
    std::function<void()> job = std::move(queue_.back().job);
    queue_.pop_back();
    ++running_;
    lock.unlock();
    job();
    lock.lock();
    --running_;
    if (queue_.empty() && running_ == 0) {
      idle_.notify_all();
    }
  }
}

}  // namespace bibo::world
