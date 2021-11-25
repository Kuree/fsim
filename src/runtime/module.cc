#include "module.hh"

#include "fmt/format.h"
#include "marl/dag.h"
#include "scheduler.hh"

namespace xsim::runtime {
std::string Module::hierarchy_name() const {
    std::string result = std::string(inst_name);
    auto const *module = this->parent;
    while (module) {
        result = fmt::format("{0}.{1}", module->inst_name, result);
        module = module->parent;
    }

    return result;
}

bool stabilized(const std::vector<CombProcess *> &processes) {
    return std::all_of(processes.begin(), processes.end(),
                       [](auto *p) { return !p->input_changed(); });
}

void Module::active() {
    if (!comb_dag_) {
        // construct the dag
        auto builder = marl::DAGBuilder<void>();
        // we just need a linear graph
        auto node = builder.root();
        for (auto *p : comb_processes_) {
            node = node.then(p->func);
        }
        comb_dag_ = std::move(builder.build());
    }
    while (!stabilized(comb_processes_)) {
        comb_dag_->run();
    }
}

}  // namespace xsim::runtime