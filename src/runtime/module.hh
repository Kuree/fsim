#ifndef XSIM_MODULE_HH
#define XSIM_MODULE_HH

#include <memory>
#include <string>
#include <string_view>

#include "logic/logic.hh"
namespace xsim::runtime {
class Scheduler;
struct CombProcess;
struct FFProcess;
class CombinationalGraph;

bool trigger_posedge(const logic::logic<0> &old, const logic::logic<0> &new_);
bool trigger_negedge(const logic::logic<0> &old, const logic::logic<0> &new_);

class TrackedVar {
public:
    bool changed = false;

    bool track_edge = false;
    bool should_trigger_posedge = false;
    bool should_trigger_negedge = false;

    std::vector<CombProcess *> comb_processes;
    std::vector<FFProcess *> ff_posedge_processes;
    std::vector<FFProcess *> ff_negedge_processes;

    // no copy constructor
    TrackedVar(const TrackedVar &) = delete;
    TrackedVar &operator=(const TrackedVar &) = delete;

    TrackedVar() = default;

protected:
    void trigger_process();
};

template <int msb, int lsb = msb, bool signed_ = false>
class logic_t : public logic::logic<msb, lsb, signed_>, public TrackedVar {
public:
    // t for tracking
    static auto constexpr size = logic::util::abs_diff(msb, lsb) + 1;
    // we intentionally hide the underling assign operator function
    // maybe try the type
    // TODO: change this into a virtual function and rely on slang's conversion operator
    //  to automatically convert types. thus disabling logic::logic's ability to change types
    template <int op_msb, int op_lsb, bool op_signed_>
    logic::logic<size - 1, 0, signed_> &operator=(
        const logic::logic<op_msb, op_lsb, op_signed_> &v) {
        update_value(v);
        return *this;
    }

    logic_t<size - 1, 0, signed_> &operator=(const logic_t<msb, lsb, signed_> &v) {
        update_value(v);
        return *this;
    }

    logic_t(const logic_t<msb, lsb, signed_> &v) { update_value(v); }

    explicit logic_t(const logic::logic<msb, lsb, signed_> &v) { update_value(v); }

    logic_t() : TrackedVar() {}

    template <int op_msb, int op_lsb, bool op_signed_>
    void update_value(const logic::logic<op_msb, op_lsb, op_signed_> &v) {
        if (this->match(v)) {
            changed = false;
        } else {
            // dealing with edge triggering events
            if constexpr (size == 1) {
                // only allowed for size 1 signals
                auto const new_v = v[logic::util::min(op_msb, op_lsb)];
                should_trigger_posedge = track_edge && trigger_posedge(*this, new_v);
                should_trigger_negedge = track_edge && trigger_negedge(*this, new_v);
            }
            logic::logic<msb, lsb, signed_>::operator=(v);
            changed = true;
            trigger_process();
        }
    }
};

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

protected:
    std::vector<CombProcess *> comb_processes_;
    std::vector<FFProcess *> ff_process_;

    // child instances
    std::vector<Module *> child_instances_;

private:
    std::shared_ptr<CombinationalGraph> comb_graph_;
    bool sensitivity_stable();
    bool edge_stable();

    void wait_for_timed_processes();
    void schedule_ff();
};
}  // namespace xsim::runtime

#endif  // XSIM_MODULE_HH
