#include "variable.hh"

#include "scheduler.hh"

namespace xsim::runtime {

void TrackedVar::add_posedge_process(Process *process) {
    std::lock_guard guard(process_lock_);
    posedge_process_.emplace_back(process);
}

void TrackedVar::add_negedge_process(Process *process) {
    std::lock_guard guard(process_lock_);
    negedge_processes_.emplace_back(process);
}

void TrackedVar::add_edge_process(Process *process) {
    std::lock_guard guard(process_lock_);
    edge_process_.emplace_back(process);
}

bool trigger_posedge(const logic::logic<0> &old, const logic::logic<0> &new_) {
    // LRM Table 9-2
    return ((old.nmatch(logic::logic<0>::one_()) && new_.match(logic::logic<0>::one_())) ||
            (old.match(logic::logic<0>::zero_()) && new_.nmatch(logic::logic<0>::zero_())));
}

bool trigger_negedge(const logic::logic<0> &old, const logic::logic<0> &new_) {
    // LRM Table 9-2
    return ((old.nmatch(logic::logic<0>::zero_()) && new_.match(logic::logic<0>::zero_())) ||
            (old.match(logic::logic<0>::one_()) && new_.nmatch(logic::logic<0>::one_())));
}

void TrackedVar::update_edge_trigger(const logic::logic<0> &old, const logic::logic<0> &new_) {
    should_trigger_posedge = track_edge && trigger_posedge(old, new_);
    should_trigger_negedge = track_edge && trigger_negedge(old, new_);
}

void TrackedVar::trigger_process() {
    for (auto *process : comb_processes) {
        process->should_trigger = true;
    }

    if (should_trigger_posedge) {
        for (auto *process : ff_posedge_processes) {
            process->should_trigger = true;
        }
        {
            std::lock_guard guard(process_lock_);
            for (auto *process : posedge_process_) {
                process->delay.signal();
            }
            posedge_process_.clear();
        }
    }

    if (should_trigger_negedge) {
        for (auto *process : ff_negedge_processes) {
            process->should_trigger = true;
        }
        {
            std::lock_guard guard(process_lock_);
            for (auto *process : negedge_processes_) {
                process->delay.signal();
            }
            negedge_processes_.clear();
        }
    }

    if (should_trigger_negedge || should_trigger_posedge) {
        std::lock_guard guard(process_lock_);
        for (auto *process : edge_process_) {
            process->delay.signal();
        }
        edge_process_.clear();
    }
}
}  // namespace xsim::runtime