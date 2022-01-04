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
class Scheduler;
class TrackedVar;

struct Process {
    uint64_t id = 0;
    bool finished = false;
    // by default a process is always running. it is only false
    // when it is waiting for some time slot in the future
    std::atomic<bool> running = true;
    marl::Event cond = marl::Event(marl::Event::Mode::Auto);
    std::function<void()> func;

    marl::Event delay = marl::Event(marl::Event::Mode::Auto);
    Scheduler *scheduler = nullptr;

    void schedule_nba(const std::function<void()> &f) const;

    // used by trigger-based processes
    bool should_trigger = false;

    enum class EdgeControlType { posedge, negedge, both };
    struct EdgeControl {
        EdgeControlType type = EdgeControlType::posedge;
        TrackedVar *var = nullptr;
    };

    EdgeControl edge_control;
};

struct InitialProcess : public Process {};

struct ForkProcess : public Process {};

struct CombProcess : public Process {
    CombProcess();
};

struct FFProcess : public Process {
public:
    FFProcess();
};

struct FinalProcess : public Process {};

class ScheduledTimeslot {
public:
    // we statically allocate the event cond
    ScheduledTimeslot(uint64_t time, Process *process);

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
    CombProcess *create_comb_process();
    FFProcess *create_ff_process();

    static void schedule_init(InitialProcess *init);
    static void schedule_final(FinalProcess *final);
    void schedule_delay(const ScheduledTimeslot &event);
    void schedule_finish(int code);
    void schedule_nba(const std::function<void()> &func);
    void add_tracked_var(TrackedVar *var) { tracked_vars_.emplace(var); }
    void add_process_edge_control(Process *process);

    [[nodiscard]] bool finished() const { return finish_flag_.load() || terminate_; }

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
    std::vector<std::unique_ptr<FFProcess>> ff_processes_;
    marl::Scheduler marl_scheduler_;

    // NBA
    std::vector<std::function<void()>> nbas_;
    std::mutex nba_lock_;

    // finish info
    std::atomic<bool> finish_flag_ = false;
    bool terminate_ = false;
    FinishInfo finish_ = {};

    // used for track event controls that requires a stabilized synchronization
    // no mutex lock since it's computed during initialization time, which is serial
    std::unordered_set<TrackedVar *> tracked_vars_;
    std::vector<Process *> process_edge_controls_;

    std::atomic<uint64_t> id_count_ = 0;

    [[nodiscard]] bool loop_stabilized() const;
    [[nodiscard]] bool terminate() const;
    [[nodiscard]] bool execute_nba();
    void terminate_processes();

    void active();
    void stabilize_process();
    void handle_edge_triggering();

    Module *top_ = nullptr;
};
}  // namespace xsim::runtime

#endif  // XSIM_SCHEDULER_HH
