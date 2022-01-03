#include "dpi.hh"

#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>

#include "util.hh"

namespace fs = std::filesystem;

namespace xsim {

DPILocator::DPILocator() {
    auto *ld_lib_path = std::getenv("LD_LIBRARY_PATH");
    if (!ld_lib_path) {
        // macos?
        ld_lib_path = std::getenv("DYLD_LIBRARY_PATH");
    }
    if (ld_lib_path) {
        // separate by ':'
        auto paths = string::get_tokens(ld_lib_path, ":");
        auto cwd = fs::current_path();
        for (auto const &p : paths) {
            // resolve to absolute path
            auto path = fs::absolute(p);
            lib_search_dirs_.emplace(path);
        }
    }
    // always add current directory
    auto cwd = fs::current_path();
    lib_search_dirs_.emplace(cwd.string());
}

void DPILocator::add_dpi_lib(const std::string &lib_path) {
    // need to make sure it exists
    auto p = fs::path(lib_path);
    if (p.is_relative()) {
        // if it's a relative path, search based on variables locations
        for (auto const &dir : lib_search_dirs_) {
            auto path = dir / p;
            if (fs::exists(path)) {
                libs_paths_.emplace(path.string());
            }
        }
    } else {
        if (fs::exists(p)) {
            libs_paths_.emplace(lib_path);
        }
    }
}

bool DPILocator::resolve_lib(const std::string &func_name) const {
    // C function doesn't have parameter types
    for (auto const &lib_path : libs_paths_) {
        auto *r = ::dlopen(lib_path.c_str(), RTLD_LAZY);
        if (!r) {
            return false;
        }
        auto *s = ::dlsym(r, func_name.c_str());
        if (!s) return false;
        dlclose(r);
        return true;
    }
    return false;
}

}  // namespace xsim
