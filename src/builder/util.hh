#ifndef FSIM_BUILDER_UTIL_HH
#define FSIM_BUILDER_UTIL_HH

#include <string>
#include <vector>
namespace fsim::platform {
int run(const std::vector<std::string> &commands, const std::string &working_directory);
}  // namespace fsim::platform

#endif  // FSIM_BUILDER_UTIL_HH
