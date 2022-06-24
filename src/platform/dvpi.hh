#ifndef FSIM_PLATFORM_DVPI_HH
#define FSIM_PLATFORM_DVPI_HH

#include <set>
#include <string>

namespace fsim::platform {

class DLOpenHelper {
public:
    explicit DLOpenHelper(const std::string &filename);
    DLOpenHelper(const std::string &filename, int mode);

    ~DLOpenHelper();

    [[nodiscard]] void *get_sym(const std::string &name) const;

    void *ptr = nullptr;

private:
    void load(const char *name, int mode);
};

class DPILocator {
public:
    DPILocator();

    void add_dpi_lib(const std::string &lib_path);
    [[nodiscard]] bool resolve_lib(std::string_view func_name) const;

    struct LibInfo {
        bool system_specified;
        std::string path;

        bool operator<(const LibInfo &info) const { return path < info.path; }
    };

    [[nodiscard]] const std::set<LibInfo> &lib_paths() const { return libs_paths_; }

private:
    std::set<std::string> lib_search_dirs_;
    std::set<LibInfo> libs_paths_;
};

class VPILocator {
public:
    VPILocator();

    bool add_vpi_lib(const std::string &lib_path);
    [[nodiscard]] const std::set<std::string> &lib_paths() const { return lib_paths_; }

private:
    std::set<std::string> lib_search_dirs_;
    std::set<std::string> lib_paths_;
};

}  // namespace fsim::platform

#endif  // FSIM_PLATFORM_DVPI_HH
