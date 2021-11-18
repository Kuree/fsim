#include "codegen.hh"

#include <filesystem>
#include <fstream>
#include <set>

#include "fmt/format.h"

namespace xsim {

template <typename T>
inline std::string get_cc_filename(const T &name) {
    return fmt::format("{0}.cc", name);
}
template <typename T>
inline std::string get_hh_filename(const T &name) {
    return fmt::format("{0}.hh", name);
}

// need to generate header information about module declaration
// this includes variable, port, and parameter definition

auto constexpr raw_header_include = R"(#pragma once
#include "logic/array.hh"
#include "logic/logic.hh"
#include "logic/struct.hh"
#include "logic/union.hh"
#include "module.hh"
#include "system_task.hh"

// forward declaration
namespace xsim::runtime {
class Scheduler;
}

// for logic we use using literal namespace to make codegen simpler
using namespace logic::literal;

)";

std::string_view get_indent(int indent_level) {
    static std::unordered_map<int, std::string> cache;
    if (cache.find(indent_level) == cache.end()) {
        std::stringstream ss;
        for (auto i = 0; i < indent_level; i++) ss << "    ";
    }
    return cache.at(indent_level);
}

void output_header_file(const std::filesystem::path &filename, const Module *mod) {
    // analyze the dependencies to include which headers
    std::ofstream s(filename);
    int indent_level = 0;
    s << raw_header_include;

    s << get_indent(indent_level) << "class " << mod->name << ": public Module {" << std::endl;
    s << get_indent(indent_level) << "public: " << std::endl;
    indent_level++;

    // init function
    if (!mod->init_processes.empty()) {
        s << get_indent(indent_level) << "void init(xsim::runtime::Scheduler *scheduler) override;"
          << std::endl;
    }

    indent_level--;

    s << get_indent(indent_level) << "};";
}

void output_cc_file(const std::filesystem::path &filename, const Module *mod) {
    std::ofstream s(filename);
    auto hh_filename = get_hh_filename(mod->name);
    s << "#include \"" << hh_filename << "\"" << std::endl << std::endl;

    // compute init fiber conditional variables. each init process will have one variable,
    // which will be signalled if the init process finishes
}

void CXXCodeGen::output(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto cc_filename = dir_path / get_cc_filename(top_->name);
    auto hh_filename = dir_path / get_hh_filename(top_->name);
    output_header_file(hh_filename, top_);
    output_cc_file(cc_filename, top_);
}

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

void NinjaCodeGen::output(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto ninja_filename = dir_path / "build.ninja";
    std::ofstream stream(dir, std::ios::trunc);
    // filling out missing information
    // use the output dir as the runtime dir
    if (options_.runtime_path.empty()) {
        auto p = std::filesystem::path(dir);
        options_.runtime_path = p.parent_path();
    }
    if (options_.clang_path.empty()) {
        // hope for the best?
        options_.clang_path = "clang";
    }
    if (options_.binary_name.empty()) {
        options_.binary_name = default_output_name;
    }

    std::filesystem::path runtime_dir = options_.runtime_path;
    auto include_dir = runtime_dir / "include";
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
    stream << "build " << options_.binary_name << ": main " << main_name << std::endl;
}

}  // namespace xsim