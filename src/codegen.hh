#ifndef XSIM_CODEGEN_HH
#define XSIM_CODEGEN_HH
#include <utility>

#include "ir.hh"

namespace xsim {

// module is an illegal name in SV. using it will guarantee that
// there is no conflicts
auto constexpr main_name = "module.cc";
auto constexpr default_output_name = "xsim.out";

struct CXXCodeGenOptions {
    bool use_4state = true;
    bool add_vpi = false;
};

class CXXCodeGen {
public:
    CXXCodeGen(const Module *top, CXXCodeGenOptions &option)
        : top_(top), option_(option) {}

    void output(const std::string &dir);

private:
    const Module *top_;
    CXXCodeGenOptions &option_;
};

struct NinjaCodeGenOptions {
    bool debug_build = false;
    std::string clang_path;
    std::string binary_name;
};

class NinjaCodeGen {
    // generates native ninja code (bypass cmake)
public:
    NinjaCodeGen(const Module *top, NinjaCodeGenOptions &option)
        : top_(top), options_(option) {}

    void output(const std::string &dir);

private:
    const Module *top_;
    NinjaCodeGenOptions &options_;
};

}  // namespace xsim

#endif  // XSIM_CODEGEN_HH
