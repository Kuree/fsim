#include "variable.hh"

#include "scheduler.hh"

namespace fsim::runtime {

void TrackedVar::reset() {
    should_trigger_negedge = false;
    should_trigger_posedge = false;
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
    }

    if (should_trigger_negedge) {
        for (auto *process : ff_negedge_processes) {
            process->should_trigger = true;
        }
    }
}
}  // namespace fsim::runtime