#ifndef BIBO_WORLD_PARALLEL_HPP_
#define BIBO_WORLD_PARALLEL_HPP_
#include <algorithm>
#include <thread>
#include <vector>

namespace bibo {
namespace world {
/**
 * Calls body(index) for every index in [0, count) on a few threads (at most 8)
 * and waits for them. The calls must be independent of each other, e.g. each
 * one writes only its own row of a grid.
 */
template <typename Body>
void ParallelFor(int count, const Body& body) {
  const int thread_count =
      std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1,
                 std::clamp(count, 1, 8));
  std::vector<std::jthread> threads;
  for (int t = 1; t < thread_count; ++t) {
    threads.emplace_back([&body, t, thread_count, count] {
      for (int index = t; index < count; index += thread_count) {
        body(index);
      }
    });
  }
  // This thread does its share too
  for (int index = 0; index < count; index += thread_count) {
    body(index);
  }
}  // the jthreads join here
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_PARALLEL_HPP_
