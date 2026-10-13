#include "WorkerPool.hpp"
#include <algorithm>

using namespace bibo::world;

WorkerPool::WorkerPool(unsigned int threadCount)
{
	if (threadCount == 0)
	{
		unsigned int processors = std::thread::hardware_concurrency();
		threadCount = std::clamp(processors > 1 ? processors - 1 : 1u, 1u, 6u);
	}
	for (unsigned int i = 0; i < threadCount; ++i)
	{
		threads.emplace_back([this] { run(); });
	}
}

WorkerPool::~WorkerPool()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		stopping = true;
		queue.clear();
	}
	wake.notify_all();
	for (std::thread& thread : threads)
	{
		thread.join();
	}
}

void WorkerPool::submit(std::function<void()> job, float priority)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		queue.push_back({ priority, submitted++, std::move(job) });
		std::push_heap(queue.begin(), queue.end());
	}
	wake.notify_one();
}

void WorkerPool::cancelQueued()
{
	std::lock_guard<std::mutex> lock(mutex);
	queue.clear();
	if (running == 0)
	{
		idle.notify_all();
	}
}

void WorkerPool::waitIdle()
{
	std::unique_lock<std::mutex> lock(mutex);
	idle.wait(lock, [this] { return queue.empty() && running == 0; });
}

std::size_t WorkerPool::getQueuedCount()
{
	std::lock_guard<std::mutex> lock(mutex);
	return queue.size();
}

void WorkerPool::run()
{
	std::unique_lock<std::mutex> lock(mutex);
	while (true)
	{
		wake.wait(lock, [this] { return stopping || !queue.empty(); });
		if (stopping)
		{
			return;
		}
		std::pop_heap(queue.begin(), queue.end());
		std::function<void()> job = std::move(queue.back().job);
		queue.pop_back();
		++running;
		lock.unlock();
		job();
		lock.lock();
		--running;
		if (queue.empty() && running == 0)
		{
			idle.notify_all();
		}
	}
}
