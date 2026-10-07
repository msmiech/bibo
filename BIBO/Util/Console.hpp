#pragma once
#include <iostream>
#include <limits>

namespace bibo
{
	namespace util
	{
		/**
		* Portable replacement for system("PAUSE"): keeps the console window open so that error messages can be read
		*/
		inline void pause()
		{
			std::cerr << "Press Enter to continue..." << std::endl;
			std::cin.clear();
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
		}
	}
}
