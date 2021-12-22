#include "util.hh"

#include <filesystem>
#include <fstream>

namespace xsim {
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

}  // namespace xsim