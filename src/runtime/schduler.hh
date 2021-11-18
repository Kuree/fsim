#ifndef XSIM_SCHDULER_HH
#define XSIM_SCHDULER_HH

#include <stdexcept>
#include <mutex>
#include <queue>
#include "marl/event.h"
#include "marl/scheduler.h"

namespace xsim::runtime {
class SchedulingPrimitive {
    uint64_t target_time;

};

struct InitialProcess {
    uint64_t id = 0;
    bool finished = false;
    marl::Event cond = marl::Event(marl::Event::Mode::Manual);
    std::function<void()> func;
};

class Module;

class Scheduler {
public:
    Scheduler();
    void run(Module *top);

    uint64_t sim_time = 0;

    void schedule_init(const std::shared_ptr<InitialProcess>& init);

private:
    // if performance becomes a concern, use this concurrent queue instead
    // https://github.com/cameron314/concurrentqueue

    std::vector<std::shared_ptr<InitialProcess>> init_processes_;
    marl::Scheduler marl_scheduler_;
};

class FinishException: std::exception {
    // used to indicate the finish
};

}

#endif  // XSIM_SCHDULER_HH
