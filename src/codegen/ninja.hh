#ifndef FSIM_NINJA_HH
#define FSIM_NINJA_HH

#include "../ir/ir.hh"

namespace fsim {
class DPILocator;

struct NinjaCodeGenOptions {
    uint8_t optimization_level = 3;
    std::string cxx_path;
    std::string binary_name;

    std::vector<std::string> sv_libs;
};

class NinjaCodeGen {
    // generates native ninja code (bypass cmake)
public:
    NinjaCodeGen(const Module *top, NinjaCodeGenOptions &options, const DPILocator *dpi)
        : top_(top), options_(options), dpi_(dpi) {}

    void output(const std::string &dir);

private:
    const Module *top_;
    NinjaCodeGenOptions &options_;
    const DPILocator *dpi_ = nullptr;
};
}  // namespace fsim

#endif  // FSIM_NINJA_HH
