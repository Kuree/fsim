#ifndef FSIM_MACRO_HH
#define FSIM_MACRO_HH

// make the codegen more readable
#define SCHEDULE_DELAY(process, pound_time, scheduler, next_time)                        \
    do {                                                                                 \
        auto next_time =                                                                 \
            fsim::runtime::ScheduledTimeslot(scheduler->sim_time + pound_time, process); \
        scheduler->schedule_delay(next_time);                                            \
        process->cond.signal();                                                          \
        process->delay.wait();                                                           \
        if (scheduler->finished()) return;                                               \
    } while (0)

#define END_PROCESS(process)             \
    do {                                 \
        process->cond.signal();          \
        process->finished = true;        \
        process->running = false;        \
        process->should_trigger = false; \
    } while (0)

#define SCHEDULE_NBA(target, value, process)                          \
    do {                                                              \
        if (!target.match(value)) {                                   \
            auto wire = value;                                        \
            process->schedule_nba([this, wire]() { target = wire; }); \
        }                                                             \
    } while (0)

#define SCHEDULE_EDGE(process, variable, edge_type) \
    do {                                            \
        process->edge_control.var = &variable;      \
        process->edge_control.type = edge_type;     \
        process->cond.signal();                     \
        process->delay.wait();                      \
        process->edge_control.var = nullptr;        \
    } while (0)

#define START_FORK(fork_name, num_fork)   \
    std::vector<ForkProcess *> fork_name; \
    fork_name.reserve(num_fork);

#define SCHEDULE_FORK(fork_name, process)        \
    fork_name.emplace_back(process);             \
    this->fork_processes_.emplace_back(process); \
    fsim::runtime::Scheduler::schedule_fork(process);

#define SCHEDULE_JOIN(fork_name, scheduler, process)                    \
    do {                                                                \
        process->cond.signal();                                         \
        while (true) {                                                  \
            scheduler->schedule_join_check(process);                    \
            if (std::all_of(fork_name.begin(), fork_name.end(),         \
                            [](auto const *p) { return p->finished; })) \
                break;                                                  \
            process->delay.wait();                                      \
        }                                                               \
    } while (0)

#define SCHEDULE_JOIN_ANY(fork_name, scheduler, process)                \
    do {                                                                \
        process->cond.signal();                                         \
        while (true) {                                                  \
            scheduler->schedule_join_check(process);                    \
            if (std::any_of(fork_name.begin(), fork_name.end(),         \
                            [](auto const *p) { return p->finished; })) \
                break;                                                  \
            process->delay.wait();                                      \
        }                                                               \
    } while (0)

#define SCHEDULE_JOIN_NONE(fork_name, scheduler, process) \
    do {                                                  \
        (void)fork_name;                                  \
        (void)scheduler;                                  \
        (void)process;                                    \
    } while (0)
#endif  // FSIM_MACRO_HH
