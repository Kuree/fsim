#ifndef XSIM_BUILDER_HH
#define XSIM_BUILDER_HH

#include <memory>
#include <string>
#include <vector>

namespace slang {
class Compilation;
}

namespace xsim {

class Module;
class DPILocator;

struct BuildOptions {
    std::string working_dir;
    std::string working_directory;
    bool run_after_build = false;
    // this is the same as GCC, which uses -O0
    uint8_t optimization_level = 0;
    bool use_4state = true;
    bool add_vpi = false;
    std::string cxx_path;
    std::string binary_name;
    std::string top_name;

    std::vector<std::string> sv_libs;
};

class Builder {
public:
    explicit Builder(BuildOptions options);

    void build(const Module *module) const;
    void build(slang::Compilation *unit) const;

    ~Builder();

private:
    BuildOptions options_;
    std::unique_ptr<DPILocator> dpi_locator_;
};

}  // namespace xsim

#endif  // XSIM_BUILDER_HH
