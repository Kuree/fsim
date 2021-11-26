#ifndef XSIM_SCHEDULER_HH
#define XSIM_SCHEDULER_HH

#include <atomic>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>

#include "marl/event.h"
#include "marl/scheduler.h"

namespace xsim::runtime {

class Module;

struct Process {
    uint64_t id = 0;
    std::atomic<bool> finished = false;
    // by default a process is always running. it is only false
    // when it is waiting for some time slot in the future
    std::atomic<bool> running = true;
    marl::Event cond = marl::Event(marl::Event::Mode::Auto);
    std::function<void()> func;

    marl::Event delay = marl::Event(marl::Event::Mode::Auto);
};

struct InitialProcess : public Process {};

struct ForkProcess : public Process {};

struct CombProcess : public Process {
public:
    explicit CombProcess(Module *module) : module_(module) {}
    // calling this can potentially change the interval values
    [[nodiscard]] virtual bool input_changed() = 0;

protected:
    Module *module_;
};

struct FFProcess : public Process {};

struct FinalProcess : public Process {};

class ScheduledTimeslot {
public:
    // we statically allocate the event cond
    explicit ScheduledTimeslot(uint64_t time, Process *process);

    uint64_t time = 0;
    Process *process;

    bool operator<(const ScheduledTimeslot &other) const { return time > other.time; }
};

struct FinishInfo {
    int code;
};

class Scheduler {
public:
    Scheduler();
    void run(Module *top);

    uint64_t sim_time = 0;

    InitialProcess *create_init_process();
    FinalProcess *create_final_process();
    CombProcess *create_comb_process(std::unique_ptr<CombProcess> process);

    static void schedule_init(InitialProcess *init);
    static void schedule_final(FinalProcess *final);
    void schedule_delay(const ScheduledTimeslot &event);
    void schedule_finish(int code);

    [[nodiscard]] bool finished() const { return finish_flag_.load(); }

    ~Scheduler();

private:
    // if performance becomes a concern, use this concurrent queue instead
    // https://github.com/cameron314/concurrentqueue
    // had to use raw pointers here since mutex is not copyable
    // we will statically allocate time slot inside each module/class
    std::priority_queue<ScheduledTimeslot> event_queue_;
    std::mutex event_queue_lock_;

    std::vector<std::unique_ptr<InitialProcess>> init_processes_;
    std::vector<std::unique_ptr<FinalProcess>> final_processes_;
    std::vector<std::unique_ptr<CombProcess>> comb_processes_;
    marl::Scheduler marl_scheduler_;

    // finish info
    std::atomic<bool> finish_flag_ = false;
    FinishInfo finish_ = {};
};
}  // namespace xsim::runtime

#endif  // XSIM_SCHEDULER_HH
