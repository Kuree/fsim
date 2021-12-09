#include "scheduler.hh"

#include <iostream>
#include <utility>

#include "module.hh"

namespace xsim::runtime {

void Process::schedule_nba(const std::function<void()> &f) const { scheduler->schedule_nba(f); }

CombProcess::CombProcess() {
    // by default, it's not running
    running = false;
    finished = true;
}

FFProcess::FFProcess() {
    // by default, it's not doing anything
    running = false;
    finished = true;
}

ScheduledTimeslot::ScheduledTimeslot(uint64_t time, Process *process)
    : time(time), process(process) {}

Scheduler::Scheduler() : marl_scheduler_(marl::Scheduler::Config::allCores()) {
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
    top_ = top;
    top->comb(this);
    top->ff(this);
    // init will run immediately so need to initialize comb and ff first
    top->init(this);
    top->final(this);
    bool finished = false;

    // either wait for the finish or wait for the complete from init
    while (true) {
        do {
            for (auto &init : init_processes_) {
                // process finished. don't care anymore
                if (init->finished) continue;
                if (!init->running) continue;
                init->cond.wait();
                init->running = false;
            }
            // active
            top->active();
            // nba
            execute_nba();
            // activate again
            top->active();
        } while (!loop_stabilized());

        if (terminate()) {
            break;
        }

        // detect finish. notice that we need a second one below in case we finish it before
        // finish is detected
        if (finish_flag_) {
            printout_finish(finish_.code, sim_time);
            finished = true;
            break;
        }

        // schedule for the next time slot
        {
            // need to lock it since the moment we unlock a process, it may try to
            // schedule more events immediately
            std::lock_guard guard(event_queue_lock_);
            if (!event_queue_.empty()) {
                auto next_slot_time = event_queue_.top().time;
                // jump to the next
                sim_time = next_slot_time;
                //  we could have multiple events scheduled at the same time slot
                //  release all of them at once
                while (!event_queue_.empty() && event_queue_.top().time == next_slot_time) {
                    auto const &event = event_queue_.top();
                    event.process->delay.signal();
                    event.process->running = true;
                    event_queue_.pop();
                }
            }
        }
    }

    if (finish_flag_ && !finished) {
        printout_finish(finish_.code, sim_time);
    }

    // execute final
    for (auto &final : final_processes_) {
        schedule_final(final.get());
    }
}

InitialProcess *Scheduler::create_init_process() {
    auto ptr = std::make_unique<InitialProcess>();
    ptr->id = id_count_.fetch_add(1);
    ptr->scheduler = this;
    auto &p = init_processes_.emplace_back(std::move(ptr));
    return p.get();
}

FinalProcess *Scheduler::create_final_process() {
    auto ptr = std::make_unique<FinalProcess>();
    ptr->id = id_count_.fetch_add(1);
    ptr->scheduler = this;
    auto &p = final_processes_.emplace_back(std::move(ptr));
    return p.get();
}

CombProcess *Scheduler::create_comb_process() {
    // scheduler will keep this comb process alive
    auto ptr = std::make_unique<CombProcess>();
    ptr->id = id_count_.fetch_add(1);
    ptr->scheduler = this;
    auto &p = comb_processes_.emplace_back(std::move(ptr));
    return p.get();
}

FFProcess *Scheduler::create_ff_process() {
    auto ptr = std::make_unique<FFProcess>();
    ptr->id = id_count_.fetch_add(1);
    ptr->scheduler = this;
    auto &p = ff_processes_.emplace_back(std::move(ptr));
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
    finish_flag_ = true;
}

void Scheduler::schedule_nba(const std::function<void()> &func) {
    std::lock_guard guard(nba_lock_);
    nbas_.emplace_back(func);
}

Scheduler::~Scheduler() {
    nbas_.clear();
    marl_scheduler_.unbind();  // NOLINT
}

bool Scheduler::loop_stabilized() const {
    auto r = top_->stabilized();
    return r;
}

bool Scheduler::terminate() const {
    return !has_init_left(init_processes_) && top_->stabilized() && event_queue_.empty();
}

void Scheduler::execute_nba() {
    // maybe split it up into multiple fiber threads?
    for (auto const &f : nbas_) {
        f();
    }
    nbas_.clear();
}

}  // namespace xsim::runtime