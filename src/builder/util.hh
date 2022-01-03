#ifndef XSIM_BUILDER_UTIL_HH
#define XSIM_BUILDER_UTIL_HH

#include <string>
#include <vector>

namespace xsim::string {
std::vector<std::string> get_tokens(const std::string &line, const std::string &delimiter);
}

#endif  // XSIM_BUILDER_UTIL_HH
