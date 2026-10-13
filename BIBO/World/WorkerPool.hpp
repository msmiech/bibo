#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace bibo
{
	namespace world
	{
		/**
		* Threads that do jobs in the background (generating the world while the game runs).
		* The job with the lowest priority value is done first.
		*/
		class WorkerPool
		{
		public:
			/**
			* @param threadCount 0: one thread less than the processor has (at least 1, at most 6)
			*/
			explicit WorkerPool(unsigned int threadCount = 0);
			/**
			* Drops the jobs that did not start and waits for the running ones
			*/
			~WorkerPool();
			WorkerPool(const WorkerPool&) = delete;
			WorkerPool& operator=(const WorkerPool&) = delete;

			void submit(std::function<void()> job, float priority);
			/**
			* Drops the jobs that did not start yet
			*/
			void cancelQueued();
			/**
			* Waits until all jobs are done
			*/
			void waitIdle();
			std::size_t getQueuedCount();
			unsigned int getThreadCount() const { return static_cast<unsigned int>(threads.size()); }

		private:
			struct Entry
			{
				float priority;
				std::uint64_t order; //first come, first served for the same priority
				std::function<void()> job;
				bool operator<(const Entry& other) const
				{
					//std::push_heap makes a max heap: the "largest" entry is the one to do next
					return priority != other.priority ? priority > other.priority : order > other.order;
				}
			};

			void run();

			std::mutex mutex;
			std::condition_variable wake, idle;
			std::vector<Entry> queue; //heap
			std::vector<std::thread> threads;
			std::uint64_t submitted = 0;
			int running = 0;
			bool stopping = false;
		};
	}
}
