#ifndef XSIM_MODULE_HH
#define XSIM_MODULE_HH

#include <memory>
#include <string>
#include <string_view>

#include "logic/logic.hh"
namespace xsim::runtime {
class Scheduler;
class CombProcess;
class FFProcess;
class CombinationalGraph;

void schedule_callbacks(const std::vector<FFProcess *> &processes);
bool trigger_posedge(const logic::logic<0> &old, const logic::logic<0> &new_);
bool trigger_negedge(const logic::logic<0> &old, const logic::logic<0> &new_);

template <int msb, int lsb = msb, bool signed_ = false>
class logic_t : public logic::logic<msb, lsb, signed_> {
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
        if (this->match(v)) {
            changed = false;
        } else {
            // dealing with edge triggering events
            if constexpr (size == 1) {
                // only allowed for size 1 signals
                // LRM Table 9-2
                auto const new_v = v[logic::util::min(op_msb, op_lsb)];
                bool should_trigger_posedge = !posedge.empty() && trigger_posedge(*this, new_v);
                bool should_trigger_negedge = !negedge.empty() && trigger_negedge(*this, new_v);
                // value has to be changed first
                logic::logic<msb, lsb, signed_>::operator=(v);
                if (should_trigger_posedge) schedule_callbacks(posedge);
                if (should_trigger_negedge) schedule_callbacks(negedge);
                changed = true;
            } else {
                // value has to be changed first
                logic::logic<msb, lsb, signed_>::operator=(v);
                changed = true;
            }
        }

        return *this;
    }

    // discard the state when it's assigned to
    // clang-tidy will complain, but it's worth it
    bool changed = false;

    std::vector<FFProcess *> posedge;
    std::vector<FFProcess *> negedge;
};

class Module {
public:
    Module() = delete;
    explicit Module(std::string_view def_name) : def_name(def_name), inst_name(def_name) {}
    explicit Module(std::string_view def_name, std::string_view inst_name)
        : def_name(def_name), inst_name(inst_name) {}
    virtual void init(Scheduler *) {}
    virtual void comb(Scheduler *) {}
    virtual void ff(Scheduler *) {}
    virtual void nba(Scheduler *) {}
    virtual void final(Scheduler *) {}

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

private:
    std::shared_ptr<CombinationalGraph> comb_graph_;
    bool sensitivity_stable();

    void wait_for_timed_processes();
};
}  // namespace xsim::runtime

#endif  // XSIM_MODULE_HH
