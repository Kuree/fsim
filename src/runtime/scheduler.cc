#include "scheduler.hh"

#include <iostream>
#include <utility>

#include "module.hh"
#include "variable.hh"
#include "vpi.hh"

namespace fsim::runtime {

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

ScheduledJoin::ScheduledJoin(const std::vector<const ForkProcess *> &process,
                             Process *parent_process, JoinType type)
    : processes(process), parent_process(parent_process), type(type) {}

Scheduler::Scheduler() : marl_scheduler_(marl::Scheduler::Config::allCores()) {
    // bind to the main thread
    marl_scheduler_.bind();
}

inline bool has_init_left(const std::vector<std::unique_ptr<InitialProcess>> &inits) {
    return std::any_of(inits.begin(), inits.end(), [](auto const &i) { return !i->finished; });
}

void printout_finish(int code, uint64_t time, std::string_view loc) {
    std::cout << "$finish(" << code << ") called at " << time;
    if (!loc.empty()) {
        std::cout << " (" << loc << ")";
    }
    std::cout << std::endl;
}

inline void wake_up_thread(Process *process) {
    process->running = true;
    process->delay.signal();
}

bool join_processes(std::vector<ScheduledJoin> &joins) {
    bool changed = false;
    std::vector<ScheduledJoin> new_joins;
    for (auto const &join : joins) {
        switch (join.type) {
            case ScheduledJoin::JoinType::All: {
                auto r = std::all_of(join.processes.begin(), join.processes.end(),
                                     [](auto const *p) { return p->finished; });
                if (r) {
                    changed = true;
                    wake_up_thread(join.parent_process);
                } else {
                    new_joins.emplace_back(join);
                }
                break;
            }
            case ScheduledJoin::JoinType::Any: {
                auto r = std::any_of(join.processes.begin(), join.processes.end(),
                                     [](auto const *p) { return p->finished; });
                if (r) {
                    changed = true;
                    wake_up_thread(join.parent_process);
                } else {
                    new_joins.emplace_back(join);
                }
                break;
            }
            case ScheduledJoin::JoinType::None: {
                // noop
                changed = true;
                wake_up_thread(join.parent_process);
                break;
            }
        }
    }
    joins.clear();
    joins = std::vector(new_joins.begin(), new_joins.end());
    return changed;
}

void Scheduler::run(Module *top) {
    // schedule init for every module
    top_ = top;
    top->comb(this);
    top->ff(this);

    // start of the simulation
    if (vpi_) vpi_->start();

    // init will run immediately so need to initialize comb and ff first
    top->init(this);
    top->final(this);

    // either wait for the finish or wait for the complete from init
    while (true) {
    start:
        do {
            // active
            active();
            // nba
            bool has_changed = execute_nba();
            if (has_changed) [[likely]] {
                active();
            }
        } while (!loop_stabilized());

        if (terminate()) {
            terminate_ = true;
            break;
        }

        {
            // check if there is any process waiting for join
            // at this point it's stable, so we don't need a lock to check
            if (!join_processes_.empty()) {
                std::lock_guard guard(join_processes_lock_);
                auto changed = join_processes(join_processes_);
                if (changed) goto start;
            }
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
                    wake_up_thread(event.process);
                    event_queue_.pop();
                }
            }
        }
    }

    // stop any processes that's still running
    // this only happens when a process has infinite loop or waiting for the next event schedule
    terminate_ = true;
    terminate_processes();

    // execute final
    for (auto &final : final_processes_) {
        schedule_final(final.get());
    }

    // end of simulation
    if (vpi_) vpi_->end();
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

ForkProcess *Scheduler::create_fork_process() {
    auto ptr = std::make_unique<ForkProcess>();
    ptr->id = id_count_.fetch_add(1);
    ptr->scheduler = this;
    auto &p = fork_processes_.emplace_back(std::move(ptr));
    return p.get();
}

void Scheduler::schedule_init(InitialProcess *process) {
    process->running = true;
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

void Scheduler::schedule_join_check(const ScheduledJoin &join) {
    std::lock_guard guard(join_processes_lock_);
    join_processes_.emplace_back(join);
}

void Scheduler::schedule_fork(ForkProcess *process) {
    process->running = true;
    marl::schedule([process] { process->func(); });
}

void Scheduler::schedule_finish(int code, std::string_view loc) {
    if (!finish_flag_) {
        finish_.code = code;
        finish_.loc = loc;
        finish_flag_ = true;
    }
}

void Scheduler::schedule_nba(const std::function<void()> &func) {
    std::lock_guard guard(nba_lock_);
    nbas_.emplace_back(func);
}

void Scheduler::add_process_edge_control(Process *process) {
    process_edge_controls_.emplace_back(process);
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
    return finish_flag_.load() ||
           (!has_init_left(init_processes_) && top_->stabilized() && event_queue_.empty());
}

bool Scheduler::execute_nba() {
    // maybe split it up into multiple fiber threads?
    bool has_nba = !nbas_.empty();
    for (auto const &f : nbas_) {
        f();
    }
    nbas_.clear();
    return has_nba;
}

void Scheduler::terminate_processes() {
    terminate_ = true;
    for (auto &process : init_processes_) {
        if (!process->finished) {
            // cancel the delay immediately. notice that the finish flag has been set
            process->delay.signal();
        }
    }

    // same thing for other processes as well
    for (auto &process : comb_processes_) {
        if (!process->finished) {
            process->delay.signal();
        }
    }

    for (auto &process : ff_processes_) {
        if (!process->finished) {
            process->delay.signal();
        }
    }

    for (auto &process : fork_processes_) {
        if (!process->finished) {
            process->delay.signal();
        }
    }

    if (finish_flag_) {
        printout_finish(finish_.code, sim_time, finish_.loc);
    }
}

template <typename T>
requires(std::is_base_of<Process, T>::value) void settle_processes(
    const std::vector<std::unique_ptr<T>> &processes) {
    for (auto &p : processes) {
        // process finished. don't care anymore
        if (p->finished) continue;
        if (!p->running) continue;
        p->cond.wait();
        p->running = false;
    }
}

void Scheduler::active() {
    // need to wait for all processes settled
    stabilize_process();
    top_->active();
    stabilize_process();

    handle_edge_triggering();
    stabilize_process();
}

void Scheduler::stabilize_process() {
    settle_processes(init_processes_);
    settle_processes(comb_processes_);
    settle_processes(ff_processes_);
    settle_processes(fork_processes_);
}

void Scheduler::handle_edge_triggering() {
    for (auto *process : process_edge_controls_) {
        if (process->edge_control.var) {
            auto const *var = process->edge_control.var;
            bool trigger = false;
            switch (process->edge_control.type) {
                case Process::EdgeControlType::posedge:
                    trigger = var->should_trigger_posedge;
                    break;
                case Process::EdgeControlType::negedge:
                    trigger = var->should_trigger_negedge;
                    break;
                case Process::EdgeControlType::both:
                    trigger = var->should_trigger_negedge || var->should_trigger_posedge;
                    break;
            }
            if (trigger) {
                process->running = true;
                process->delay.signal();
            }
        }
    }
    // this is necessary due to the out of ordering of execution
    for (auto *tracked_var : tracked_vars_) {
        tracked_var->reset();
    }
}

}  // namespace fsim::runtime