#ifndef XSIM_SCHEDULER_HH
#define XSIM_SCHEDULER_HH

#include <atomic>
#include <mutex>
#include <queue>
#include <stdexcept>

#include "marl/event.h"
#include "marl/scheduler.h"

namespace xsim::runtime {

struct Process {
    uint64_t id = 0;
    std::atomic<bool> finished = false;
    marl::Event cond = marl::Event(marl::Event::Mode::Manual);
    std::function<void()> func;

    marl::Event delay = marl::Event(marl::Event::Mode::Manual);
};

struct InitialProcess : public Process {};

struct ForkProcess : public Process {};

class Module;

class ScheduledTimeslot {
public:
    // we statically allocate the event cond
    explicit ScheduledTimeslot(uint64_t time, std::shared_ptr<Process> process);

    uint64_t time = 0;
    std::shared_ptr<Process> process;

    bool operator<(const ScheduledTimeslot &other) const { return time < other.time; }
};

class Scheduler {
public:
    Scheduler();
    void run(Module *top);

    uint64_t sim_time = 0;

    void schedule_init(const std::shared_ptr<InitialProcess> &init);
    void schedule_delay(const ScheduledTimeslot &event);

    ~Scheduler();

private:
    // if performance becomes a concern, use this concurrent queue instead
    // https://github.com/cameron314/concurrentqueue
    // had to use raw pointers here since mutex is not copyable
    // we will statically allocate time slot inside each module/class
    std::priority_queue<ScheduledTimeslot> event_queue_;
    std::mutex event_queue_lock_;

    std::vector<std::shared_ptr<InitialProcess>> init_processes_;
    marl::Scheduler marl_scheduler_;
};

class FinishException : std::exception {
    // used to indicate the finish
public:
    explicit FinishException(int code) : code(code) {}

    int code;
};

}  // namespace xsim::runtime

#endif  // XSIM_SCHEDULER_HH
