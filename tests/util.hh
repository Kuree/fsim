#ifndef XSIM_UTIL_HH
#define XSIM_UTIL_HH

#include "subprocess.hpp"

void build_c_shared_lib(const std::string &c_content, const std::string &output_name) {
    // pipe in
    auto pipe =
        subprocess::Popen(std::vector<std::string>{"cc", "-xc", "-shared", "-o", output_name, "-"},
                          subprocess::input(subprocess::PIPE));
    pipe.send(c_content.c_str(), c_content.size());
    pipe.communicate();
    pipe.wait();
}

#endif  // XSIM_UTIL_HH
