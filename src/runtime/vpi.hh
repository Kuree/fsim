#ifndef FSIM_VPI_HH
#define FSIM_VPI_HH

#include <set>

#include "module.hh"

namespace fsim::runtime {

class VPIController {
    // responsible to deal with all kinds of VPI calls
public:
    void set_args(int argc, char *argv[]);

    // because VPi is C interface, we need a singleton
    static VPIController *get_vpi();
    void set_top(Module *top) { top_ = top; }
    [[nodiscard]] const std::vector<char *> &get_args() const { return args_; }

    // simulation related control
    void start();
    void end();

    // load vpi startups
    static void load(std::string_view lib_path);

    ~VPIController();

private:
    std::vector<char *> args_;
    Module *top_;
    std::set<void *> vpi_libs_;

    static std::unique_ptr<VPIController> vpi_;
};

}  // namespace fsim::runtime

#endif  // FSIM_VPI_HH
