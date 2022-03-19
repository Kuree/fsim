#ifndef XSIM_VPI_HH
#define XSIM_VPI_HH

#include "module.hh"

namespace xsim::runtime {

class VPIController {
    // responsible to deal with all kinds of VPI calls
public:
    void set_args(int argc, char *argv[]);

    // because VPi is C interface, we need a singleton
    static VPIController *get_vpi();
    void set_top(Module *top) { top_ = top; }
    [[nodiscard]] const std::vector<char *> &get_args() const { return args_; }

private:
    std::vector<char *> args_;
    Module *top_;

    static std::unique_ptr<VPIController> vpi_;
};

}  // namespace xsim::runtime

#endif  // XSIM_VPI_HH
