#include "ninja.hh"

#include <filesystem>
#include <set>

#include "../builder/dvpi.hh"
#include "fmt/format.h"
#include "util.hh"

namespace fsim {

std::set<std::string_view> get_defs(const Module *module) {
    auto defs = module->get_defs();
    std::set<std::string_view> result;
    for (auto const *def : defs) {
        result.emplace(def->name);
    }
    return result;
}

std::string get_linker_flags(const DPILocator *dpi) {
    if (!dpi) return {};
    auto const &libs = dpi->lib_paths();
    std::vector<std::string> lib_flags;
    for (auto const &lib : libs) {
        if (lib.system_specified) continue;
        auto res = fmt::format("{0} -Wl,-rpath,{0}", lib.path);
        lib_flags.emplace_back(res);
    }
    auto res = fmt::join(lib_flags, " ");
    // potentially a bug in the fmt, need to change it from lvalue to rvalue
    // NOLINTNEXTLINE
    return fmt::format("{0}", std::move(res));
}

void NinjaCodeGen::output(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto ninja_filename = dir_path / "build.ninja";
    std::stringstream stream;
    // filling out missing information
    // use the output dir as the runtime dir
    if (options_.cxx_path.empty()) {
        // hope for the best?
        auto const *cxx = std::getenv("FSIM_CXX");
        if (cxx) {
            options_.cxx_path = cxx;
        } else {
            options_.cxx_path = "g++";
        }
    }
    if (options_.binary_name.empty()) {
        options_.binary_name = default_output_name;
    }

    std::filesystem::path runtime_dir = std::filesystem::absolute(dir);
    auto include_dir = runtime_dir / "include";
    auto lib_path = runtime_dir / "lib";
    auto runtime_lib_path = lib_path / "libfsim-runtime.so";
    stream << "cflags = -I" << include_dir << " -std=c++20 -march=native -m64 ";
    auto level = std::clamp<uint32_t>(options_.optimization_level, 0, 3);
    stream << "-O" << level << " ";
    if (level == 0) {
        stream << "-g ";
    }
    // ignore warning flags for apple clang
    stream << "-Wno-unknown-attributes ";
    stream << std::endl << std::endl;
    // codegen rules
    stream << "rule cc" << std::endl;
    stream << "  depfile = $out.d" << std::endl;
    stream << "  command = " << options_.cxx_path << " -MD -MF $out.d $cflags -c $in -o $out"
           << std::endl
           << "  description = $out" << std::endl
           << std::endl;

    // output for each module definition
    auto defs = get_defs(top_);
    std::string objs;
    // add main object as well
    defs.emplace(main_name);
    for (auto const &name : defs) {
        auto obj_name = fmt::format("{0}.o", name);
        stream << "build " << obj_name << ": cc " << get_cc_filename(name) << std::endl;
        objs.append(obj_name).append(" ");
    }
    auto main_linkers = fmt::format("-pthread -lstdc++ -Wl,-rpath,{0} ", lib_path.string());
    auto dpi_linkers = get_linker_flags(dpi_);
    main_linkers.append(dpi_linkers);
    // build the main
    stream << "rule main" << std::endl;
    stream << "  command = " << options_.cxx_path << " $in " << runtime_lib_path << " $cflags "
           << main_linkers << " -o $out" << std::endl
           << "  description = $out" << std::endl
           << std::endl;
    stream << "build " << options_.binary_name << ": main " << objs << std::endl;

    write_to_file(ninja_filename, stream);
}
}  // namespace fsim