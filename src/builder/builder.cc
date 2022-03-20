#include "builder.hh"

#include <filesystem>
#include <iostream>
#include <unordered_set>

#include "../codegen/cxx.hh"
#include "../codegen/ninja.hh"
#include "../ir/except.hh"
#include "dvpi.hh"
#include "fmt/format.h"
#include "slang/compilation/Compilation.h"
#include "slang/symbols/ASTVisitor.h"
#include "slang/syntax/AllSyntax.h"
#include "subprocess.hpp"

namespace fsim {

auto constexpr default_working_dir = "fsim_dir";

void symlink_folders(const std::string &output_dir, const std::string &simv_path) {
    // need to locate where the files are
    // for now we look for stuff based on the current file
    std::filesystem::path runtime, logic, marl, runtime_path;
    bool in_tree_build = simv_path.empty();
    if (!in_tree_build) {
        // if it's in the tools folder
        std::filesystem::path exe = simv_path;
        auto root = exe.parent_path().filename();
        if (root == "tools") {
            in_tree_build = true;
        }
    }
    if (in_tree_build) {
        std::filesystem::path current_file = __FILE__;
        // builder is in its own folder
        auto src = current_file.parent_path().parent_path();
        runtime = src / "runtime";
        auto root = src.parent_path();
        auto extern_ = root / "extern";
        logic = extern_ / "logic" / "include" / "logic";
        marl = extern_ / "marl" / "include" / "marl";

        std::filesystem::directory_iterator it(root);
        for (auto const &p : it) {
            std::string path_str = p.path();
            if (path_str.find("build") != std::string::npos) {
                auto target_path = p.path() / "src" / "runtime" / "libfsim-runtime.so";
                if (std::filesystem::exists(target_path)) {
                    runtime_path = target_path;
                }
            }
        }
        if (runtime_path.empty()) {
            throw std::runtime_error("Unable to locate fsim runtime library");
        }

    } else {
        std::filesystem::path exe = simv_path;
        auto root = exe.parent_path().parent_path();
        auto include = root / "include";
        runtime = include / "runtime";
        logic = include / "logic";
        marl = include / "marl";
        runtime_path = root / "lib" / "libfsim-runtime.so";
    }

    if (!std::filesystem::exists(runtime_path)) {
        throw std::runtime_error("Unable to locate fsim libraries");
    }

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
    auto runtime_dst_path = dst_lib_dir / "libfsim-runtime.so";
    if (!std::filesystem::exists(runtime_dst_path)) {
        std::filesystem::create_symlink(runtime_path, runtime_dst_path);
    }
}

class DPIFunctionVisitor : public slang::ASTVisitor<DPIFunctionVisitor, true, true> {
public:
    [[maybe_unused]] void handle(const slang::CallExpression &expr) {
        auto kind = expr.getSubroutineKind();
        if (kind == slang::SubroutineKind::Function) {
            // dpi can only be function
            slang::bitmask<slang::MethodFlags> flags;
            const slang::SubroutineSymbol *sym;
            if (expr.subroutine.index() == 0) {
                sym = std::get<0>(expr.subroutine);
                flags = sym->flags;
            } else {
                // this could be VPI, or regular system call
                return;
            }
            if (flags.has(slang::MethodFlags::DPIImport)) {
                names.emplace(expr.getSubroutineName(), &expr);
            }
            if (flags.has(slang::MethodFlags::DPIContext)) {
                throw NotSupportedException("Context DPI function not supported", sym->location);
            }
        }
    }

    std::unordered_map<std::string_view, const slang::CallExpression *> names;
};

std::unordered_map<std::string_view, const slang::CallExpression *> get_all_dpi_calls(
    const Module *module) {
    DPIFunctionVisitor v;
    module->def()->visit(v);
    auto const &names = v.names;
    return names;
}

void verify_dpi_functions(DPILocator *dpi, const Module *module, const BuildOptions &options) {
    auto names = get_all_dpi_calls(module);
    for (auto const &p : options.sv_libs) {
        dpi->add_dpi_lib(p);
    }
    for (auto const &[func_name, func] : names) {
        auto exists = dpi->resolve_lib(func_name);
        if (!exists) {
            auto *sym = std::get<0>(func->subroutine);
            throw InvalidSyntaxException(fmt::format("DPI function {0} cannot be resolved given "
                                                     "current shared library setup",
                                                     func_name),
                                         sym->location);
        }
    }
}

void verify_vpi_functions(VPILocator *vpi, BuildOptions &options) {
    for (auto const &path : options.vpi_libs) {
        if (!vpi->add_vpi_lib(path)) {
            throw InvalidInput(fmt::format("{0} is not a valid VPI library", path));
        }
    }
    auto const &libs = vpi->lib_paths();
    options.vpi_libs = std::vector(libs.begin(), libs.end());
}

Builder::Builder(BuildOptions options) : options_(std::move(options)) {
    // filling up empty information
    if (options_.working_dir.empty()) {
        options_.working_dir = default_working_dir;
    }
}

void Builder::build(const Module *module) {
    if (!std::filesystem::exists(options_.working_dir)) {
        std::filesystem::create_directories(options_.working_dir);
    }
    // generate ninja first
    NinjaCodeGenOptions n_options;
    n_options.optimization_level = options_.optimization_level;
    n_options.cxx_path = options_.cxx_path;
    n_options.binary_name = options_.binary_name;
    n_options.sv_libs = options_.sv_libs;
    // check all the DPI functions to see if they are valid
    DPILocator dpi_locator;
    verify_dpi_functions(&dpi_locator, module, options_);
    // check the vpi
    VPILocator vpi_locator;
    verify_vpi_functions(&vpi_locator, options_);

    NinjaCodeGen ninja(module, n_options, &dpi_locator);
    ninja.output(options_.working_dir);

    // then generate the C++ code
    CXXCodeGenOptions c_options;
    c_options.vpi_libs = options_.vpi_libs;
    c_options.use_4state = options_.use_4state;

    // could be parallelized here
    auto modules = module->get_defs();
    for (auto const *mod : modules) {
        CXXCodeGen cxx(mod, c_options);
        cxx.output(options_.working_dir);
    }
    // output main as well
    {
        CXXCodeGen cxx(module, c_options);
        cxx.output_main(options_.working_dir);
    }

    // need to symlink stuff over
    symlink_folders(options_.working_dir, options_.working_directory);

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
        std::cout << std::endl;
        using namespace subprocess;
        auto p = call(fmt::format("./{0}", n_options.binary_name), cwd{options_.working_dir});
        if (p != 0) {
            throw InternalError("Internal simulator error");
        }
    }
}

void Builder::build(slang::Compilation *unit) {
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

void Builder::cleanup() const {
    // need to remove the binary (which could be from previous runs)
    std::filesystem::path binary_path = options_.binary_name;
    binary_path = options_.working_dir / binary_path;
    if (std::filesystem::exists(binary_path)) {
        std::filesystem::remove(binary_path);
    }
    // remove the symbolic one as well
    binary_path = options_.binary_name;
    if (std::filesystem::is_symlink(binary_path)) {
        std::filesystem::remove(binary_path);
    }
}

Builder::~Builder() = default;

}  // namespace fsim