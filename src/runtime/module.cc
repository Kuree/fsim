#include "module.hh"

#include "fmt/format.h"
#include "marl/waitgroup.h"
#include "scheduler.hh"

namespace xsim::runtime {

bool trigger_posedge(const logic::logic<0> &old, const logic::logic<0> &new_) {
    // LRM Table 9-2
    return ((old != logic::logic<0>::one_() && new_ == logic::logic<0>::one_()) ||
            (old == logic::logic<0>::zero_() && new_ != logic::logic<0>::zero_()));
}

bool trigger_negedge(const logic::logic<0> &old, const logic::logic<0> &new_) {
    // LRM Table 9-2
    return ((old != logic::logic<0>::zero_() && new_ == logic::logic<0>::zero_()) ||
            (old == logic::logic<0>::one_() && new_ != logic::logic<0>::one_()));
}

void TrackedVar::trigger_process() {
    for (auto *process : comb_processes) {
        process->should_trigger = true;
    }

    if (should_trigger_posedge && !ff_posedge_processes.empty()) {
        for (auto *process : ff_posedge_processes) {
            process->should_trigger = true;
        }
    }

    if (should_trigger_negedge && !ff_negedge_processes.empty()) {
        for (auto *process : ff_negedge_processes) {
            process->should_trigger = true;
        }
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
            if (!p->finished) {
                continue;
            }
            if (p->should_trigger) {
                marl::schedule([p]() {
                    p->finished = false;
                    p->running = true;
                    p->func();
                });
                p->cond.wait();
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
        }

        while (!edge_stable()) {
            schedule_ff();
            changed = true;
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

bool Module::stabilized() const {
    auto r = std::all_of(comb_processes_.begin(), comb_processes_.end(),
                         [](auto *p) { return p->finished || !p->running; });
    r = r && std::all_of(ff_process_.begin(), ff_process_.end(),
                         [](auto *p) { return p->finished || !p->running; });

    return r;
}

void Module::wait_for_timed_processes() {
    for (auto *p : comb_processes_) {
        if (!p->finished && p->running) {
            p->cond.wait();
            p->running = false;
        }
    }

    for (auto *p : ff_process_) {
        if (!p->finished && p->running) {
            p->cond.wait();
        }
    }
}

void Module::schedule_ff() {  // NOLINT
    if (ff_process_.empty()) return;
    uint64_t num_process = 0;
    for (auto const *p : ff_process_) {
        if (p->should_trigger) num_process++;
    }
    marl::WaitGroup wg(num_process);

    // this is just to make sure we call each functions
    for (auto *p : ff_process_) {
        if (p->should_trigger) {
            marl::schedule([p, wg]() {
                p->func();
                wg.done();
            });
        }
    }

    wg.wait();

    for (auto *inst : child_instances_) {
        inst->schedule_ff();
    }

    for (auto *p : ff_process_) {
        p->cancel_changed();
    }
}

}  // namespace xsim::runtime