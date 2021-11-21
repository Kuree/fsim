#include "scheduler.hh"

#include <iostream>
#include <utility>

#include "module.hh"

namespace xsim::runtime {

// statically determined the number of cores to use based on number of event-based processes?
constexpr uint64_t num_marl_cores = 2;

ScheduledTimeslot::ScheduledTimeslot(uint64_t time, std::shared_ptr<Process> process)
    : time(time), process(std::move(process)) {}

Scheduler::Scheduler() : marl_scheduler_({num_marl_cores}) {
    // bind to the main thread
    marl_scheduler_.bind();
}

inline bool has_init_left(const std::vector<std::shared_ptr<InitialProcess>> &inits) {
    return std::any_of(inits.begin(), inits.end(), [](auto const &i) { return !i->finished; });
}

void Scheduler::run(Module *top) {
    // schedule init for every module
    top->init(this);

    // either wait for the finish or wait for the complete from init
    try {
        while (true) {
            if (!has_init_left(init_processes_)) {
                break;
            }
            for (auto &init : init_processes_) {
                init->cond.wait();
            }
            // active
            // nba

            // schedule for the next time slot
            while (!event_queue_.empty()) {
                auto next_slot_time = event_queue_.top().time;
                sim_time = next_slot_time;
                // we could have multiple events scheduled at the same time slot
                // release all of them at once
                while (!event_queue_.empty() && event_queue_.top().time == next_slot_time) {
                    auto event = event_queue_.top();
                    event_queue_.pop();
                    event.process->delay.signal();
                }
            }
        }
    } catch (const FinishException &ex) {
        std::cout << "Finish called at " << sim_time << " width status " << ex.code << std::endl;
    }
}

void Scheduler::schedule_init(const std::shared_ptr<InitialProcess> &init) {
    init->id = init_processes_.size();
    auto process = init_processes_.emplace_back(init);
    marl::schedule([process] {
        process->func();
        // notice that the done is handled by the function call side
        // if time control happens, the function will notify the cond variable,
        // then wait for the correct time or condition (using different event variable)
        // the main process still waits for the same condition variable
    });
}

void Scheduler::schedule_delay(const ScheduledTimeslot &event) {
    std::lock_guard guard(event_queue_lock_);
    event_queue_.emplace(event);
}

Scheduler::~Scheduler() {
    marl_scheduler_.unbind();  // NOLINT
}

}  // namespace xsim::runtime