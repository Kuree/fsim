#ifndef FSIM_CXX_HH
#define FSIM_CXX_HH

#include "../ir/ir.hh"
#include "util.hh"

namespace fsim {
struct CXXCodeGenOptions {
    bool use_4state = true;
    std::vector<std::string> vpi_libs;

    [[nodiscard]] bool add_vpi() const { return !vpi_libs.empty(); }
};

class CXXCodeGen {
public:
    CXXCodeGen(const Module *top, CXXCodeGenOptions &option) : top_(top), option_(option) {}

    void output(const std::string &dir);
    void output_main(const std::string &dir);

private:
    const Module *top_;
    CXXCodeGenOptions &option_;
    CodeGenModuleInformation info_;
};

}  // namespace fsim

#endif  // FSIM_CXX_HH
