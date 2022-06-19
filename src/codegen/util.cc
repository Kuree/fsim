#include "util.hh"

#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "reproc++/drain.hpp"
#include "reproc++/reproc.hpp"
#include "slang/compilation/Compilation.h"
#include "slang/text/SourceManager.h"

namespace fsim {

namespace util::fs {
std::optional<std::string> which(std::string_view name) {
    // windows is more picky
    std::string env_path;
#ifdef _WIN32
    char *path_var;
    size_t len;
    auto err = _dupenv_s(&path_var, &len, "PATH");
    if (err) {
        env_path = "";
    }
    env_path = std::string(path_var);
    free(path_var);
    path_var = nullptr;
#else
    env_path = std::getenv("PATH");
#endif
    // tokenize it base on either : or ;
    auto tokens = string::get_tokens(env_path, ";:");
    for (auto const &dir : tokens) {
        auto new_path = fs::join(dir, name);
        if (std::filesystem::exists(new_path)) {
            return new_path;
        }
    }
    return std::nullopt;
}

std::string join(std::string_view path1, std::string_view path2) {
    namespace fs = std::filesystem;
    fs::path p1 = path1;
    fs::path p2 = path2;
    return p1 / p2;
}

}  // namespace util::fs

namespace util::string {
// trim function copied from https://stackoverflow.com/a/217605
// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(),
            s.end());
}

// trim from both ends (in place)
void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

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
}  // namespace util::string

std::string format_cxx_file(const std::string &content) {
    const static bool clang_format_available = util::fs::which("clang-format").has_value();
    // clang-format not available. return as is
    if (!clang_format_available) return content;
    // pipe through the files
    reproc::process process;
    const static std::vector<const char *> args = {"clang-format", nullptr};
    auto ec = process.start(args.data());
    if (ec) {
        // error
        return content;
    }
    process.write(reinterpret_cast<const uint8_t *>(content.c_str()), content.size());
    if ((ec = process.close(reproc::stream::in))) {
        return content;
    }
    std::string output;
    reproc::sink::string sink(output);
    ec = reproc::drain(process, sink, reproc::sink::null);
    if (ec) {
        // error
        return content;
    }
    int status = 0;
    std::tie(status, ec) = process.wait(reproc::infinite);
    if (ec) {
        return content;
    }
    return output;
}

void write_to_file(const std::string &filename, std::stringstream &stream, bool format) {
    auto raw_buf = stream.str();
    auto buf = format ? format_cxx_file(raw_buf) : raw_buf;
    if (!std::filesystem::exists(filename)) {
        // doesn't exist, directly write to the file
        std::ofstream s(filename, std::ios::trunc);
        s << buf;
    } else {
        // need to compare size
        // if they are different, output to that file
        {
            std::ifstream f(filename, std::ifstream::ate | std::ifstream::binary);
            auto size = static_cast<uint64_t>(f.tellg());
            if (size != buf.size()) {
                std::ofstream s(filename, std::ios::trunc);
                s << buf;
            } else {
                // they are the same size, now need to compare its actual content
                std::string contents;
                f.seekg(0, std::ios::end);
                contents.resize(f.tellg());
                f.seekg(0, std::ios::beg);
                f.read(&contents[0], static_cast<int64_t>(contents.size()));

                if (contents != buf) {
                    f.close();
                    std::ofstream s(filename, std::ios::trunc);
                    s << buf;
                }
            }
        }
    }
}

std::string CodeGenModuleInformation::get_new_name(const std::string &prefix, bool track_new_name) {
    std::string suffix;
    uint64_t count = 0;
    while (true) {
        auto result = fmt::format("{0}{1}", prefix, suffix);
        if (used_names_.find(result) == used_names_.end()) {
            if (track_new_name) {
                used_names_.emplace(result);
            }
            return result;
        }
        suffix = std::to_string(count++);
    }
}

std::string CodeGenModuleInformation::enter_process() {
    auto name = get_new_name("process");
    process_names_.emplace(name);
    return name;
}

const std::string &CodeGenModuleInformation::scheduler_name() {
    if (scheduler_name_.empty()) {
        scheduler_name_ = get_new_name("scheduler");
    }
    return scheduler_name_;
}

void CodeGenModuleInformation::exit_process() {
    auto name = current_process_name();
    // before we create a new scope for every process, when it exists the scope, we can
    // recycle the process name
    used_names_.erase(name);
    process_names_.pop();
}

const slang::Compilation *CodeGenModuleInformation::get_compilation() const {
    return current_module ? current_module->get_compilation() : nullptr;
}

inline bool valid_cxx_name(std::string_view name) {
    if (name.empty()) return false;
    auto first = name[0];
    if (first != '_' && !std::isalpha(first)) return false;
    return std::all_of(name.begin() + 1, name.end(),
                       [](auto c) { return c == '_' || std::isdigit(c) || std::isalpha(c); });
}

std::string_view CodeGenModuleInformation::get_identifier_name(std::string_view name) {
    if (valid_cxx_name(name)) [[likely]] {
        return name;
    } else {
        if (renamed_identifier_.find(name) == renamed_identifier_.end()) {
            auto new_name = get_new_name("renamed_var");
            renamed_identifier_.emplace(name, new_name);
        }
        return renamed_identifier_.at(name);
    }
}

std::pair<std::string_view, uint32_t> get_loc(const slang::SourceLocation &loc,
                                              const slang::Compilation *compilation) {
    if (!compilation) return {};
    auto const *sm = compilation->getSourceManager();
    auto filename = sm->getFileName(loc);
    auto line_num = sm->getLineNumber(loc);
    return {filename, line_num};
}

}  // namespace fsim
