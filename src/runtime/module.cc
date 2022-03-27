#include "module.hh"

#include "fmt/format.h"
#include "marl/waitgroup.h"
#include "scheduler.hh"
#include "variable.hh"

namespace fsim::runtime {

// Module static members
std::mutex Module::cout_lock_;

inline void wait_process_switch(Process *process) {
    process->cond.wait();
    process->running = false;
}

inline void start_process(Process *process) {
    process->finished = false;
    process->running = true;
}

inline bool should_trigger_process(Process *process) {
    return process->should_trigger && process->finished;
}

// NOLINTNEXTLINE
void Module::init(Scheduler *scheduler) {
    for (auto *inst : child_instances_) {
        inst->init(scheduler);
    }
}

// NOLINTNEXTLINE
void Module::comb(Scheduler *scheduler) {
    for (auto *inst : child_instances_) {
        inst->comb(scheduler);
    }
}

// NOLINTNEXTLINE
void Module::ff(Scheduler *scheduler) {
    for (auto *inst : child_instances_) {
        inst->ff(scheduler);
    }
}

// NOLINTNEXTLINE
void Module::final(Scheduler *scheduler) {
    for (auto *inst : child_instances_) {
        inst->final(scheduler);
    }
}

std::string Module::hierarchy_name() const {
    std::string result = std::string(inst_name);
    auto const *module = this->parent;
    while (module) {
        result = fmt::format("{0}.{1}", module->inst_name, result);
        module = module->parent;
    }

    return result;
}

class CombinationalGraph {
public:
    explicit CombinationalGraph(const std::vector<CombProcess *> &comb_processes)
        : comb_processes_(comb_processes) {}

    void run() {
        for (auto *p : comb_processes_) {
            // if it's not finished, it means it's waiting
            if (should_trigger_process(p)) {
                start_process(p);
                marl::schedule([p]() { p->func(); });
                wait_process_switch(p);
            }
        }
    }

private:
    const std::vector<CombProcess *> &comb_processes_;
};

void Module::active() {  // NOLINT
    if (!comb_graph_) {
        comb_graph_ = std::make_shared<CombinationalGraph>(comb_processes_);
        // every process is finished by default
        for (auto *p : comb_processes_) {
            p->finished = true;
        }
    }

    bool changed;
    do {
        // try to finish what's still there
        wait_for_timed_processes();

        changed = false;
        while (!sensitivity_stable()) {
            comb_graph_->run();
            for (auto *inst : child_instances_) {
                inst->active();
            }
            changed = true;
            wait_for_timed_processes();
        }

        while (!edge_stable()) {
            // try to finish what's still there
            schedule_ff();
            changed = true;

            wait_for_timed_processes();
        }
    } while (changed);
}

bool Module::sensitivity_stable() {
    if (comb_processes_.empty()) return true;
    auto r = std::all_of(comb_processes_.begin(), comb_processes_.end(), [](auto *p) {
        return !p->should_trigger || (!p->running && !p->finished);
    });
    return r;
}

bool Module::edge_stable() {
    return std::all_of(ff_process_.begin(), ff_process_.end(),
                       [](auto *p) { return !p->should_trigger || (!p->running && !p->finished); });
}

bool Module::stabilized() const {  // NOLINT
    auto r = std::all_of(comb_processes_.begin(), comb_processes_.end(),
                         [](auto *p) { return p->finished || !p->running; });
    r = r && std::all_of(ff_process_.begin(), ff_process_.end(),
                         [](auto *p) { return p->finished || !p->running; });

    r = r && std::all_of(init_processes_.begin(), init_processes_.end(),
                         [](auto *p) { return p->finished || !p->running; });

    r = r && std::all_of(fork_processes_.begin(), fork_processes_.end(),
                         [](auto *p) { return p->finished || !p->running; });

    if (!r) return r;

    return std::all_of(child_instances_.begin(), child_instances_.end(),
                       [](auto *i) { return i->stabilized(); });
}

void Module::wait_for_timed_processes() {
    for (auto *p : comb_processes_) {
        if (!p->finished && p->running) {
            wait_process_switch(p);
        }
    }

    for (auto *p : ff_process_) {
        if (!p->finished && p->running) {
            wait_process_switch(p);
        }
    }

    for (auto *p : fork_processes_) {
        if (!p->finished && p->running) {
            wait_process_switch(p);
        }
    }
}

void Module::schedule_ff() {  // NOLINT
    if (ff_process_.empty()) return;

    // this is just to make sure we call each functions
    // 1. first pass to compute the number of process to trigger
    std::vector<FFProcess *> processes;
    processes.reserve(ff_process_.size());
    for (auto *p : ff_process_) {
        if (should_trigger_process(p)) {
            processes.emplace_back(p);
        }
    }

    // 2. actually trigger the function if necessary
    if (!processes.empty()) {
        marl::WaitGroup trigger_control(processes.size());

        for (auto *p : processes) {
            start_process(p);
            marl::schedule([p, trigger_control]() {
                trigger_control.done();
                // non-blocking. we only use the wait group to make sure that the function
                // actually gets scheduled before we return the call
                p->func();
            });
        }
        trigger_control.wait();
    }
}

}  // namespace fsim::runtime