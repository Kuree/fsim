#include "schduler.hh"

namespace xsim::runtime {

// statically determined the number of cores to use based on number of event-based processes?
constexpr uint64_t num_marl_cores = 2;

Scheduler::Scheduler() : marl_scheduler_({num_marl_cores}) {
    // bind to the main thread
    marl_scheduler_.bind();
}

void Scheduler::schedule_init(const std::shared_ptr<InitialProcess>& init) {
    auto process = init_processes_.emplace_back(init);
    marl::schedule([process] {
        process->func();
        // notice that the done is handled by the function call side
        // if time control happens, the function will notify the cond variable,
        // then wait for the correct time or condition (using different event variable)
        // the main process still waits for the same condition variable
    });
}

}  // namespace xsim::runtime