#ifndef FSIM_UTIL_HH
#define FSIM_UTIL_HH

#include <filesystem>
#include <vector>
#include <chrono>

#include "reproc++/reproc.hpp"
#include "reproc++/drain.hpp"

inline void build_c_shared_lib(const std::string &c_content, const std::string &output_name) {
    using namespace std::chrono_literals;
    // find vlstd header
    auto current_file = std::filesystem::path(__FILE__);
    auto root = current_file.parent_path().parent_path();
    auto vlstd_include = root / "extern" / "vlstd";
    auto include_flag = "-I" + std::filesystem::absolute(vlstd_include).string();
    auto output_name_path = std::filesystem::path(output_name);
    // check base dir
    auto output_dir = output_name_path.parent_path();
    if (!std::filesystem::exists(output_dir)) {
        std::filesystem::create_directories(output_dir);
    }
    auto output_basename = std::filesystem::path(output_name).filename();
    reproc::options options;
    options.working_directory = output_dir.string().c_str();
    // pipe in
    reproc::process process;
    std::vector<const char *> commands = {
        "cc", "-xc", "-shared", "-o", output_basename.c_str(), include_flag.c_str(), "-", nullptr};
    process.start(commands.data(), options);
    process.write(reinterpret_cast<const uint8_t *>(c_content.c_str()), c_content.size());
    process.close(reproc::stream::in);
    std::string output;
    reproc::sink::string sink(output);
    reproc::drain(process, sink, reproc::sink::null);
    process.wait(reproc::infinite);
}

#endif  // FSIM_UTIL_HH
