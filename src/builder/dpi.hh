#ifndef XSIM_BUILDER_DPI_HH
#define XSIM_BUILDER_DPI_HH

#include <set>
#include <string>

namespace xsim {
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
}  // namespace xsim

#endif  // XSIM_BUILDER_DPI_HH
