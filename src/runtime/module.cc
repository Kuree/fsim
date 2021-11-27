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

class CombinationalGraph {
public:
    explicit CombinationalGraph(const std::vector<CombProcess *> &processes)
        : processes_(processes) {}

    void run() {
        for (auto *p : processes_) {
            // if it's not finished, it means it's waiting
            if (!p->finished) {
                continue;
            }

            // printf("schedule comb @ %ld finished %d running %d\n", p->scheduler->sim_time,
            // p->finished.load(), p->running.load());
            marl::schedule([p]() {
                p->finished = false;
                p->running = true;
                p->func();
                p->cond.signal();
                p->finished = true;
                p->running = false;
            });
            p->cond.wait();
            p->running = false;
        }

        for (auto *p : processes_) {
            // printf("cancel changed @ %ld\n", p->scheduler->sim_time);
            p->cancel_changed();
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
            p->running = false;
            p->finished = true;
            // printf("finishing prev comb @ %ld\n", p->scheduler->sim_time);
        }
    }

    while (!stabilized_()) {
        comb_graph_->run();
    }
}

bool Module::stabilized_() {
    auto r = std::all_of(comb_processes_.begin(), comb_processes_.end(),
                         [](auto *p) { return !p->input_changed(); });
    // printf("comb stabilized %d @ %ld\n", r, comb_processes_[0]->scheduler->sim_time);
    return r;
}

bool Module::stabilized() const {
    return std::all_of(comb_processes_.begin(), comb_processes_.end(),
                       [](auto *p) { return p->finished || !p->running; });
}

}  // namespace xsim::runtime