#ifndef XSIM_NINJA_HH
#define XSIM_NINJA_HH

#include "../ir/ir.hh"

namespace xsim {
struct NinjaCodeGenOptions {
    uint8_t optimization_level = 3;
    std::string cxx_path;
    std::string binary_name;

    std::vector<std::string> sv_libs;
};

class NinjaCodeGen {
    // generates native ninja code (bypass cmake)
public:
    NinjaCodeGen(const Module *top, NinjaCodeGenOptions &option) : top_(top), options_(option) {}

    void output(const std::string &dir);

private:
    const Module *top_;
    NinjaCodeGenOptions &options_;
};
}  // namespace xsim

#endif  // XSIM_NINJA_HH
