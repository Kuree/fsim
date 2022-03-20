#include "util.hh"

#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "slang/compilation/Compilation.h"
#include "slang/text/SourceManager.h"

namespace fsim {
void write_to_file(const std::string &filename, std::stringstream &stream) {
    if (!std::filesystem::exists(filename)) {
        // doesn't exist, directly write to the file
        std::ofstream s(filename, std::ios::trunc);
        s << stream.rdbuf();
    } else {
        // need to compare size
        // if they are different, output to that file
        {
            std::ifstream f(filename, std::ifstream::ate | std::ifstream::binary);
            auto size = static_cast<uint64_t>(f.tellg());
            auto buf = stream.str();
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

std::string_view get_indent(int indent_level) {
    static std::unordered_map<int, std::string> cache;
    if (cache.find(indent_level) == cache.end()) {
        std::stringstream ss;
        for (auto i = 0; i < indent_level; i++) ss << "    ";
        cache.emplace(indent_level, ss.str());
    }
    return cache.at(indent_level);
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
