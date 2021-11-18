#ifndef XSIM_CODEGEN_HH
#define XSIM_CODEGEN_HH
#include <utility>

#include "ir.hh"

namespace xsim {

// module is an illegal name in SV. using it will guarantee that
// there is no conflicts
auto constexpr main_name = "module.cc";
auto constexpr default_output_name = "xsim.out";

class CXXCodeGen {};

struct NinjaCodeGenOptions {
    bool debug_build = false;
    std::string runtime_path;
    std::string clang_path;
    std::string output_name;
};

class NinjaCodeGen {
    // generates native ninja code (bypass cmake)
public:
    NinjaCodeGen(const Module *top, NinjaCodeGenOptions option)
        : top_(top), options_(std::move(option)) {}

    void output(const std::string &path);

private:
    const Module *top_;
    NinjaCodeGenOptions options_;
};

}  // namespace xsim

#endif  // XSIM_CODEGEN_HH
