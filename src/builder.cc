#include "builder.hh"

#include <filesystem>
#include <iostream>
#include <unordered_set>

#include "codegen.hh"
#include "fmt/format.h"
#include "slang/compilation/Compilation.h"
#include "subprocess.hpp"

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
    auto dst_include_dir = output_path / "include";
    auto runtime_include = dst_include_dir / "runtime";
    auto logic_include = dst_include_dir / "logic";
    auto marl_include = dst_include_dir / "marl";

    auto dst_lib_dir = output_path / "lib";
    for (auto const &d : {dst_include_dir, dst_lib_dir}) {
        if (!std::filesystem::exists(d)) {
            std::filesystem::create_directories(d);
        }
    }

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> paths = {
        {runtime, runtime_include}, {logic, logic_include}, {marl, marl_include}};

    for (auto const &[src_path, dst_path] : paths) {
        if (!std::filesystem::exists(dst_path)) {
            std::filesystem::create_directory_symlink(src_path, dst_path);
        }
    }

    // need to find the final runtime build as well
    // for now searching for any folder that contains "build" and look for path
    // once packaging is working all the search process needs to be enhanced
    auto runtime_dst_path = dst_lib_dir / "libxsim-runtime.so";
    if (!std::filesystem::exists(runtime_dst_path)) {
        bool lib_linked = false;
        std::filesystem::directory_iterator it(root);
        for (auto const &p : it) {
            std::string path_str = p.path();
            if (path_str.find("build") != std::string::npos) {
                auto target_path = p.path() / "src" / "runtime" / "libxsim-runtime.so";
                if (std::filesystem::exists(target_path)) {
                    std::filesystem::create_symlink(target_path, runtime_dst_path);
                    lib_linked = true;
                    break;
                }
            }
        }
        if (!lib_linked) {
            throw std::runtime_error("Unable to locate xsim runtime library");
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
    if (!std::filesystem::exists(options_.working_dir)) {
        std::filesystem::create_directories(options_.working_dir);
    }
    // generate ninja first
    NinjaCodeGenOptions n_options;
    n_options.debug_build = options_.debug_build;
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
    {
        using namespace subprocess;
        auto p = call("ninja", cwd{options_.working_dir});
        if (p != 0) {
            return;
        }
        // symlink the output to the current directory
        if (std::filesystem::exists(n_options.binary_name)) {
            std::filesystem::remove(n_options.binary_name);
        }
        std::filesystem::path binary_path = options_.working_dir;
        binary_path = binary_path / n_options.binary_name;
        std::filesystem::create_symlink(binary_path, n_options.binary_name);
    }
    if (options_.run_after_build) {
        using namespace subprocess;
        auto p = call(fmt::format("./{0}", n_options.binary_name), cwd{options_.working_dir});
        if (p != 0) {
            return;
        }
    }
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