#include "scheduler.hh"

#include <iostream>
#include <utility>

#include "module.hh"

namespace xsim::runtime {

// statically determined the number of cores to use based on number of event-based processes?
constexpr uint64_t num_marl_cores = 2;

ScheduledTimeslot::ScheduledTimeslot(uint64_t time, Process *process)
    : time(time), process(process) {}

Scheduler::Scheduler() : marl_scheduler_({num_marl_cores}) {
    // bind to the main thread
    marl_scheduler_.bind();
}

inline bool has_init_left(const std::vector<std::unique_ptr<InitialProcess>> &inits) {
    return std::any_of(inits.begin(), inits.end(), [](auto const &i) { return !i->finished; });
}

void printout_finish(int code, uint64_t time) {
    std::cout << "$finish(" << code << ") called at " << time << std::endl;
}

void Scheduler::run(Module *top) {
    // schedule init for every module
    top->init(this);
    top->final(this);
    bool finished = false;

    // either wait for the finish or wait for the complete from init
    while (true) {
        for (auto &init : init_processes_) {
            if (init->finished) continue;
            init->cond.wait();
        }

        if (!has_init_left(init_processes_)) {
            break;
        }

        // active
        // nba

        // detect finish. notice that we need a second one below in case we finish it before
        // finish is detected
        if (finish_flag) {
            printout_finish(finish_.code, sim_time);
            finished = true;
            break;
        }

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

    if (finish_flag && !finished) {
        printout_finish(finish_.code, sim_time);
    }

    // execute final
    for (auto &final : final_processes_) {
        schedule_final(final.get());
    }
}

InitialProcess *Scheduler::create_init_process() {
    auto ptr = std::make_unique<InitialProcess>();
    ptr->id = init_processes_.size();
    auto &p = init_processes_.emplace_back(std::move(ptr));
    return p.get();
}

FinalProcess *Scheduler::create_final_process() {
    auto ptr = std::make_unique<FinalProcess>();
    ptr->id = final_processes_.size();
    auto &p = final_processes_.emplace_back(std::move(ptr));
    return p.get();
}

void Scheduler::schedule_init(InitialProcess *process) {
    marl::schedule([process] {
        process->func();
        // notice that the done is handled by the function call side
        // if time control happens, the function will notify the cond variable,
        // then wait for the correct time or condition (using different event variable)
        // the main process still waits for the same condition variable
    });
}

void Scheduler::schedule_final(FinalProcess *final) {
    // we don't suspect that the final process is going to take a long time
    // in single threaded mode
    final->func();
}

void Scheduler::schedule_delay(const ScheduledTimeslot &event) {
    std::lock_guard guard(event_queue_lock_);
    event_queue_.emplace(event);
}

void Scheduler::schedule_finish(int code) {
    finish_.code = code;
    finish_flag = true;
}

Scheduler::~Scheduler() {
    marl_scheduler_.unbind();  // NOLINT
}

}  // namespace xsim::runtime