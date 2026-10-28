#ifndef BIBO_WORLD_WORKERPOOL_HPP_
#define BIBO_WORLD_WORKERPOOL_HPP_
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace bibo {
namespace world {
/**
 * Threads that do jobs in the background (generating the world while the game
 * runs). The job with the lowest priority value is done first.
 */
class WorkerPool {
 public:
  /**
   * @param thread_count 0: one thread less than the processor has (at least 1,
   * at most 6)
   */
  explicit WorkerPool(unsigned int thread_count = 0);
  /**
   * Drops the jobs that did not start and waits for the running ones
   */
  ~WorkerPool();
  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  void Submit(std::function<void()> job, float priority);
  /**
   * Drops the jobs that did not start yet
   */
  void CancelQueued();
  /**
   * Waits until all jobs are done
   */
  void WaitIdle();
  std::size_t GetQueuedCount();
  unsigned int GetThreadCount() const {
    return static_cast<unsigned int>(threads_.size());
  }

 private:
  struct Entry {
    float priority;
    std::uint64_t order;  // first come, first served for the same priority
    std::function<void()> job;
    bool operator<(const Entry& other) const {
      // std::push_heap makes a max heap: the "largest" entry is the one to do
      // next
      return priority != other.priority ? priority > other.priority
                                        : order > other.order;
    }
  };

  void Run();

  std::mutex mutex_;
  std::condition_variable wake_, idle_;
  std::vector<Entry> queue_;  // heap
  std::vector<std::thread> threads_;
  std::uint64_t submitted_ = 0;
  int running_ = 0;
  bool stopping_ = false;
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_WORKERPOOL_HPP_
