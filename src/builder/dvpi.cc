#include "dvpi.hh"

#ifdef _WIN32
#include <Windows.h>
#define RTLD_LAZY 0
#else
#include <dlfcn.h>
#endif

#include <cstdlib>
#include <filesystem>

#include "util.hh"

namespace fs = std::filesystem;

namespace fsim {

namespace platform {
DLOpenHelper::DLOpenHelper(const std::string &filename) { load(filename.c_str(), RTLD_LAZY); }

DLOpenHelper::DLOpenHelper(const std::string &filename, int mode) { load(filename.c_str(), mode); }

void DLOpenHelper::load(const char *name, int mode) {
#ifdef _WIN32
    ptr = LoadLibrary(name);
    (void)mode;
#else
    ptr = ::dlopen(name, mode);
#endif
}

DLOpenHelper::~DLOpenHelper() {
    if (ptr) {
#ifdef _WIN32
        FreeLibrary((HMODULE)ptr);
#else
        dlclose(ptr);
#endif
    }
}

[[nodiscard]] void *DLOpenHelper::get_sym(const std::string &name) const {
    if (!ptr) return nullptr;
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)ptr, name.c_str());
#else
    return ::dlsym(ptr, name.c_str());
#endif
}

}  // namespace platform

std::set<std::string> get_lib_search_path() {
    std::set<std::string> res;
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
            res.emplace(path.string());
        }
    }
    // always add current directory
    auto cwd = fs::current_path();
    res.emplace(cwd.string());
    return res;
}

DPILocator::DPILocator() { lib_search_dirs_ = get_lib_search_path(); }

void DPILocator::add_dpi_lib(const std::string &lib_path) {
    // need to make sure it exists
    auto p = fs::path(lib_path);
    if (p.is_relative()) {
        if (fs::exists(p)) {
            libs_paths_.emplace(LibInfo{false, fs::absolute(p).string()});
        } else {
            // if it's a relative path, search based on variables locations
            for (auto const &dir : lib_search_dirs_) {
                auto path = dir / p;
                if (fs::exists(path)) {
                    libs_paths_.emplace(LibInfo{true, path.string()});
                    return;
                }
            }
        }
    } else {
        if (fs::exists(p)) {
            libs_paths_.emplace(LibInfo{true, lib_path});
        }
    }
}

bool DPILocator::resolve_lib(std::string_view func_name) const {
    // C function doesn't have parameter types
    for (auto const &lib_info : libs_paths_) {
        platform::DLOpenHelper dl(lib_info.path);
        auto *r = dl.ptr;
        if (!r) {
            return false;
        }
        std::string name = std::string(func_name);
        auto *s = dl.get_sym(name);
        if (!s) return false;
        return true;
    }
    return false;
}

VPILocator::VPILocator() { lib_search_dirs_ = get_lib_search_path(); }

bool VPILocator::add_vpi_lib(const std::string &lib_path) {
    // based-off LRM 36.9.1
    // here we only check if there is valid registration
    // need to make sure it exists
    std::string resolved_lib_path;
    auto p = fs::path(lib_path);
    if (p.is_relative()) {
        if (fs::exists(p)) {
            resolved_lib_path = fs::absolute(p).string();
        } else {
            // if it's a relative path, search based on variables locations
            for (auto const &dir : lib_search_dirs_) {
                auto path = dir / p;
                if (fs::exists(path)) {
                    resolved_lib_path = path.string();
                    break;
                }
            }
        }
    } else {
        if (fs::exists(p)) {
            resolved_lib_path = lib_path;
        }
    }
    if (resolved_lib_path.empty()) return false;
    // need to check if the startup routine exists
    constexpr auto var_name = "vlog_startup_routines";
    platform::DLOpenHelper dl(resolved_lib_path);
    auto *r = dl.ptr;
    if (!r) {
        return false;
    }
    auto *s = dl.get_sym(var_name);
    auto res = s != nullptr;
    if (res) {
        lib_paths_.emplace(resolved_lib_path);
    }
    return res;
}

}  // namespace fsim
