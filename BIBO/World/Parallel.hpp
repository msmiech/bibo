#pragma once
#include <algorithm>
#include <thread>
#include <vector>

namespace bibo
{
	namespace world
	{
		/**
		* Calls body(index) for every index in [0, count) on a few threads (at most 8) and waits for them.
		* The calls must be independent of each other, e.g. each one writes only its own row of a grid.
		*/
		template <typename Body>
		void parallelFor(int count, const Body& body)
		{
			const int threadCount = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, std::clamp(count, 1, 8));
			std::vector<std::jthread> threads;
			for (int t = 1; t < threadCount; ++t)
			{
				threads.emplace_back([&body, t, threadCount, count] {
					for (int index = t; index < count; index += threadCount)
					{
						body(index);
					}
				});
			}
			//This thread does its share too
			for (int index = 0; index < count; index += threadCount)
			{
				body(index);
			}
		} //the jthreads join here
	}
}
