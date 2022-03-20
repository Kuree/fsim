#ifndef FSIM_BUILDER_UTIL_HH
#define FSIM_BUILDER_UTIL_HH

#include <string>
#include <vector>

namespace fsim::string {
std::vector<std::string> get_tokens(const std::string &line, const std::string &delimiter);
}

#endif  // FSIM_BUILDER_UTIL_HH
