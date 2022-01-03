#ifndef XSIM_DPI_HH
#define XSIM_DPI_HH

#include <set>
#include <string>

namespace xsim {
class DPILocator {
public:
    DPILocator();

    void add_dpi_lib(const std::string &lib_path);
    [[nodsicatd]] bool resolve_lib(const std::string &func_name) const;

private:
    std::set<std::string> lib_search_dirs_;
    std::set<std::string> libs_paths_;
};
}

#endif  // XSIM_DPI_HH
