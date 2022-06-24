#include "util.hh"

#include "reproc++/run.hpp"

namespace fsim::platform {
int run(const std::vector<std::string> &commands, const std::string &working_directory) {
    reproc::options options;
    options.working_directory = working_directory.c_str();
    int status = -1;
    std::error_code ec;
    std::vector<const char *> args;
    args.reserve(commands.size() + 1);
    for (auto const &cmd : commands) {
        args.emplace_back(cmd.c_str());
    }
    args.emplace_back(nullptr);
    std::tie(status, ec) = reproc::run(args.data(), options);
    if (ec)
        return -1;
    else
        return status;
}
}  // namespace fsim::platform