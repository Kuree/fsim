#include "codegen.hh"

#include <filesystem>
#include <fstream>
#include <set>

#include "fmt/format.h"

namespace xsim {

inline std::string get_cc_filename(const std::string &name) { return fmt::format("{0}.cc", name); }

void get_defs(const Module *module, std::set<std::string> &result) {
    result.emplace(module->name);
    for (auto const &[_, inst] : module->child_instances) {
        get_defs(inst.get(), result);
    }
}

std::set<std::string> get_defs(const Module *module) {
    std::set<std::string> result;
    get_defs(module, result);
    return result;
}

void NinjaCodeGen::output(const std::string &path) {
    std::ofstream stream(path, std::ios::trunc);
    // filling out missing information
    // use the output path as the runtime path
    if (options_.runtime_path.empty()) {
        auto p = std::filesystem::path(path);
        options_.runtime_path = p.parent_path();
    }
    if (options_.clang_path.empty()) {
        // hope for the best?
        options_.clang_path = "clang";
    }
    if (options_.output_name.empty()) {
        options_.output_name = default_output_name;
    }

    std::filesystem::path dir = options_.runtime_path;
    auto include_dir = dir / "include";
    stream << "cflags = -I" << include_dir << " ";
    if (options_.debug_build) {
        stream << "-O0 -g ";
    } else {
        stream << "-O3";
    }
    stream << std::endl << std::endl;
    stream << "rule cc" << std::endl;
    stream << "  depfile = $out.d" << std::endl;
    stream << "  command = " << options_.clang_path << " -MD -MF $out.d $cflags -c $in -o $out"
           << std::endl
           << std::endl;

    // output for each module definition
    auto defs = get_defs(top_);
    std::string objs;
    for (auto const &name : defs) {
        auto obj_name = fmt::format("{0}.o", name);
        stream << "build " << obj_name << ": cc " << get_cc_filename(name) << std::endl;
        objs.append(obj_name).append(" ");
    }
    // build the main
    stream << "rule main" << std::endl;
    stream << "  " << options_.clang_path << "$in " << objs << "$cflags -o $out" << std::endl
           << std::endl;
    stream << "build " << options_.output_name << ": main " << main_name << std::endl;
}

}  // namespace xsim