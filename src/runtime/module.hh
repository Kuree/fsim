#ifndef XSIM_MODULE_HH
#define XSIM_MODULE_HH

#include <string_view>

namespace xsim::runtime {
class Scheduler;
class Module {
public:
    Module() = delete;
    explicit Module(std::string_view module) : module(module) {}
    virtual void init(Scheduler *) {}
    virtual void comb(Scheduler *) {}
    virtual void nba(Scheduler *) {}
    virtual void final(Scheduler *) {}

    std::string_view module;
};
}  // namespace xsim::runtime

#endif  // XSIM_MODULE_HH
