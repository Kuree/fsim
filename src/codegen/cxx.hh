#ifndef XSIM_CXX_HH
#define XSIM_CXX_HH

#include "../ir/ir.hh"

namespace xsim {
struct CXXCodeGenOptions {
    bool use_4state = true;
    bool add_vpi = false;
};

class CXXCodeGen {
public:
    CXXCodeGen(const Module *top, CXXCodeGenOptions &option) : top_(top), option_(option) {}

    void output(const std::string &dir);
    void output_main(const std::string &dir);

private:
    const Module *top_;
    CXXCodeGenOptions &option_;
};

}  // namespace xsim

#endif  // XSIM_CXX_HH
