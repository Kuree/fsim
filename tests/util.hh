#ifndef FSIM_UTIL_HH
#define FSIM_UTIL_HH

#include <filesystem>

#include "reproc++/reproc.hpp"

inline void build_c_shared_lib(const std::string &c_content, const std::string &output_name) {
    // find vlstd header
    auto current_file = std::filesystem::path(__FILE__);
    auto root = current_file.parent_path().parent_path();
    auto vlstd_include = root / "extern" / "vlstd";
    auto include_flag = "-I" + std::filesystem::absolute(vlstd_include).string();
    {
        // check base dir
        auto output_dir = std::filesystem::path(output_name).parent_path();
        if (!std::filesystem::exists(output_dir)) {
            std::filesystem::create_directories(output_dir);
        }
    }
    // pipe in
    reproc::process process;
    std::vector<const char *> commands = {
        "cc", "-xc", "-shared", "-o", output_name.c_str(), include_flag.c_str(), "-", nullptr};
    process.start(commands.data());
    process.write(reinterpret_cast<const uint8_t *>(c_content.c_str()), c_content.size());
    process.close(reproc::stream::in);
    process.wait(1000ms);
}

#endif  // FSIM_UTIL_HH
