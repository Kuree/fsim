#ifndef XSIM_BUILDER_HH
#define XSIM_BUILDER_HH

#include <string>

namespace slang {
class Compilation;
}

namespace xsim {

class Module;

struct BuildOptions {
    std::string working_dir;
    bool run_after_build = false;
    bool debug_build = false;
    bool use_4state = true;
    bool add_vpi = false;
    std::string runtime_path;
    std::string clang_path;
    std::string binary_name;
    std::string top_name;
};

class Builder {
public:
    explicit Builder(BuildOptions options);

    void build(const Module *module) const;
    void build(slang::Compilation *unit) const;

private:
    BuildOptions options_;
};

}  // namespace xsim

#endif  // XSIM_BUILDER_HH
