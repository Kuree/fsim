#ifndef XSIM_UTIL_HH
#define XSIM_UTIL_HH

#include <filesystem>

#include "subprocess.hpp"

inline void build_c_shared_lib(const std::string &c_content, const std::string &output_name) {
    // find vlstd header
    auto current_file = std::filesystem::path(__FILE__);
    auto root = current_file.parent_path().parent_path();
    auto vlstd_include = root / "extern" / "vlstd";
    auto include_flag = "-I" + std::filesystem::absolute(vlstd_include).string();
    // pipe in
    auto pipe = subprocess::Popen(
        std::vector<std::string>{"cc", "-xc", "-shared", "-o", output_name, include_flag, "-"},
        subprocess::input(subprocess::PIPE));
    pipe.send(c_content.c_str(), c_content.size());
    pipe.communicate();
    pipe.wait();
}

#endif  // XSIM_UTIL_HH
