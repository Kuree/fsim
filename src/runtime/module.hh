#ifndef FSIM_MODULE_HH
#define FSIM_MODULE_HH

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {
class Scheduler;
struct CombProcess;
struct FFProcess;
struct InitialProcess;
struct ForkProcess;
class CombinationalGraph;

class Module {
public:
    Module() = delete;
    explicit Module(std::string_view def_name) : def_name(def_name), inst_name(def_name) {}
    explicit Module(std::string_view def_name, std::string_view inst_name)
        : def_name(def_name), inst_name(inst_name) {}
    virtual void init(Scheduler *scheduler);
    virtual void comb(Scheduler *scheduler);
    virtual void ff(Scheduler *scheduler);
    virtual void final(Scheduler *scheduler);

    std::string_view def_name;
    std::string_view inst_name;

    Module *parent = nullptr;

    [[nodiscard]] std::string hierarchy_name() const;

    // active region
    void active();
    [[nodiscard]] bool stabilized() const;

    // cout locks
    static void cout_lock() { cout_lock_.lock(); }
    static void cout_unlock() { cout_lock_.unlock(); }

protected:
    std::vector<CombProcess *> comb_processes_;
    std::vector<FFProcess *> ff_process_;
    std::vector<InitialProcess *> init_processes_;
    std::vector<ForkProcess *> fork_processes_;

    // child instances
    std::vector<Module *> child_instances_;

private:
    std::shared_ptr<CombinationalGraph> comb_graph_;
    bool sensitivity_stable();
    bool edge_stable();

    void wait_for_timed_processes();
    void schedule_ff();

    static std::mutex cout_lock_;
};
}  // namespace fsim::runtime

#endif  // FSIM_MODULE_HH
