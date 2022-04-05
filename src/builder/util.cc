#include "util.hh"

#include "reproc++/run.hpp"

namespace fsim {

namespace string {
std::vector<std::string> get_tokens(const std::string &line, const std::string &delimiter) {
    std::vector<std::string> tokens;
    size_t prev = 0, pos;
    std::string token;
    // copied from https://stackoverflow.com/a/7621814
    while ((pos = line.find_first_of(delimiter, prev)) != std::string::npos) {
        if (pos > prev) {
            tokens.emplace_back(line.substr(prev, pos - prev));
        }
        prev = pos + 1;
    }
    if (prev < line.length()) tokens.emplace_back(line.substr(prev, std::string::npos));
    // remove empty ones
    std::vector<std::string> result;
    result.reserve(tokens.size());
    for (auto const &t : tokens)
        if (!t.empty()) result.emplace_back(t);
    return result;
}
}  // namespace string

namespace platform {
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
}  // namespace platform

}  // namespace fsim