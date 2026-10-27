#pragma once
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace bibo {
namespace util {
/**
 * Portable replacement for system("PAUSE"): keeps the console window open so
 * that error messages can be read
 */
inline void pause() {
  std::cerr << "Press Enter to continue..." << std::endl;
  std::cin.clear();
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

/**
 * Prints the error and exits, e.g. when an asset the game cannot run without is
 * missing
 */
[[noreturn]] inline void fatalError(const std::string& message) {
  std::cerr << "ERROR: " << message << std::endl;
  pause();
  std::exit(EXIT_FAILURE);
}
}  // namespace util
}  // namespace bibo
