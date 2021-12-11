#include "variable.hh"

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

void TrackedVar::update_edge_trigger(const logic::logic<0> &old, const logic::logic<0> &new_) {
    should_trigger_posedge = track_edge && trigger_posedge(old, new_);
    should_trigger_negedge = track_edge && trigger_negedge(old, new_);
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
}  // namespace xsim::runtime