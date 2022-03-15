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

private:
    std::set<std::string> lib_search_dirs_;
    std::set<std::string> libs_paths_;
};
}  // namespace xsim

#endif  // XSIM_BUILDER_DPI_HH
