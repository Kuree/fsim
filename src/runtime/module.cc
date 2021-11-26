#include "module.hh"

#include "fmt/format.h"
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
                       [](auto *p) { return !p->input_changed() || !p->running; });
}

class CombinationalGraph {
public:
    explicit CombinationalGraph(const std::vector<CombProcess *> &processes)
        : processes_(processes) {}

    void run() {
        for (auto *p : processes_) {
            // if it's not running, it means it's waiting
            if (!p->running) {
                continue;
            }

            marl::schedule([p]() {
                p->finished = false;
                p->func();
                p->cond.signal();
                p->finished = true;
            });
            p->cond.wait();
        }
    }

private:
    const std::vector<CombProcess *> &processes_;
};

void Module::active() {
    if (!comb_graph_) {
        comb_graph_ = std::make_shared<CombinationalGraph>(comb_processes_);
        // every process is finished by default
        for (auto *p : comb_processes_) {
            p->finished = true;
        }
    }

    // try to finish what's still there
    for (auto *p : comb_processes_) {
        if (!p->finished && p->running) {
            p->cond.wait();
        }
    }

    while (!stabilized(comb_processes_)) {
        comb_graph_->run();
    }
}

}  // namespace xsim::runtime