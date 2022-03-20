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

#endif  // FSIM_MACRO_HH
