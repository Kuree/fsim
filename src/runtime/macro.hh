#ifndef XSIM_MACRO_HH
#define XSIM_MACRO_HH

// make the codegen more readable
#define SCHEDULE_DELAY(process, pound_time, scheduler, next_time)                        \
    do {                                                                                 \
        auto next_time =                                                                 \
            xsim::runtime::ScheduledTimeslot(scheduler->sim_time + pound_time, process); \
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

#endif  // XSIM_MACRO_HH
