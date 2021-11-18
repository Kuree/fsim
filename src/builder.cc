#include "builder.hh"

#include <filesystem>
#include <iostream>
#include <unordered_set>

#include "codegen.hh"
#include "slang/compilation/Compilation.h"

namespace xsim {

auto constexpr default_working_dir = "xsim_dir";

void get_defs(const Module *module, std::unordered_set<const Module *> &result) {
    result.emplace(module);
    for (auto const &[_, inst] : module->child_instances) {
        get_defs(inst.get(), result);
    }
}

std::unordered_set<const Module *> get_defs(const Module *module) {
    std::unordered_set<const Module *> result;
    get_defs(module, result);
    return result;
}

void symlink_folders(const std::string &output_dir) {
    // need to locate where the files are
    // for now we look for stuff based on the current file
    std::filesystem::path current_file = __FILE__;
    auto src = current_file.parent_path();
    auto runtime = src / "runtime";
    auto root = src.parent_path();
    auto extern_ = root / "extern";
    auto logic = extern_ / "logic" / "include" / "logic";
    auto marl = extern_ / "marl" / "include" / "marl";

    std::filesystem::path output_path = output_dir;
    auto dst_dir = output_path / "include";
    if (!std::filesystem::exists(dst_dir)) {
        std::filesystem::create_directories(dst_dir);
    }
    auto runtime_include = dst_dir / "runtime";
    auto logic_include = dst_dir / "logic";
    auto marl_include = dst_dir / "marl";

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> paths = {
        {runtime, runtime_include}, {logic, logic_include}, {marl, marl_include}};

    for (auto const &[src_path, dst_path] : paths) {
        if (!std::filesystem::exists(dst_path)) {
            std::filesystem::create_directory_symlink(src_path, dst_path);
        }
    }
}

Builder::Builder(BuildOptions options) : options_(std::move(options)) {
    // filling up empty information
    if (options_.working_dir.empty()) {
        options_.working_dir = default_working_dir;
    }
}

void Builder::build(const Module *module) const {
    // generate ninja first
    NinjaCodeGenOptions n_options;
    n_options.debug_build = options_.debug_build;
    n_options.runtime_path = options_.runtime_path;
    n_options.clang_path = options_.clang_path;
    n_options.binary_name = options_.binary_name;

    NinjaCodeGen ninja(module, n_options);
    ninja.output(options_.working_dir);

    // then generate the C++ code
    CXXCodeGenOptions c_options;
    c_options.add_vpi = options_.add_vpi;
    c_options.use_4state = options_.use_4state;

    // could be parallelized here
    auto modules = get_defs(module);
    for (auto const *mod : modules) {
        CXXCodeGen cxx(mod, c_options);
        cxx.output(options_.working_dir);
    }

    // need to symlink stuff over
    symlink_folders(options_.working_dir);

    // call ninja to build the stuff
}

void Builder::build(slang::Compilation *unit) const {
    // figure the top module
    auto const &tops = unit->getRoot().topInstances;
    const slang::InstanceSymbol *inst = nullptr;
    if (tops.empty()) {
        return;
        // nothing there
    } else if (tops.size() > 1) {
        // trying to figure out which top to use
        for (auto const *i : tops) {
            if (i->name == options_.top_name) {
                inst = i;
                break;
            }
        }
    } else {
        inst = tops[0];
    }
    if (!inst && tops.size() > 1) {
        inst = tops[0];
        std::cerr << "warning: using " << inst->name << " as top" << std::endl;
    }
    Module m(inst);
    m.analyze();
    build(&m);
}

}  // namespace xsim