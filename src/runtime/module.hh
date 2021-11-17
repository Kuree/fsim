#ifndef XSIM_MODULE_HH
#define XSIM_MODULE_HH

namespace xsim::runtime {
class Scheduler;
class Module {
public:
    virtual void init(Scheduler *scheduler) {}
    virtual void comb(Scheduler *scheduler) {}
    virtual void nba(Scheduler *scheduler) {}
    virtual void final(Scheduler *scheduler) {}
};
}  // namespace xsim::runtime

#endif  // XSIM_MODULE_HH
