#ifndef XSIM_SCHDULER_HH
#define XSIM_SCHDULER_HH

#include <stdexcept>
#include <mutex>
#include <queue>

namespace xsim::runtime {
class SchedulingPrimitive {
    uint64_t target_time;

};

class Module;

class Scheduler {
public:
    void run(Module *top);

    uint64_t sim_time = 0;

private:
    // if performance becomes a concern, use this concurrent queue instead
    // https://github.com/cameron314/concurrentqueue

};

class FinishException: std::exception {
    // used to indicate the finish
};

}

#endif  // XSIM_SCHDULER_HH
