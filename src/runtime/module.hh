#ifndef XSIM_MODULE_HH
#define XSIM_MODULE_HH

#include <memory>
#include <string>
#include <string_view>

#include "logic/logic.hh"

namespace marl {
template <typename T>
class DAG;
}

namespace xsim::runtime {
class Scheduler;
class CombProcess;

template <int msb, int lsb = msb, bool signed_ = false>
class logic_t : public logic::logic<msb, lsb, signed_> {
public:
    // t for tracking
    static auto constexpr size = logic::util::abs_diff(msb, lsb) + 1;
    // we intentionally hide the underling assign operator function
    // maybe try the type
    template <int op_msb, int op_lsb, bool op_signed_>
    logic::logic<size - 1, 0, signed_> &operator=(
        const logic::logic<op_msb, op_lsb, op_signed_> &value) {
        if (this->match(value)) {
            changed = false;
        } else {
            logic::logic<msb, lsb, signed_>::operator=(value);
            changed = true;
        }
        return *this;
    }

    // discard the state when it's assigned to
    // clang-tidy will complain, but it's worth it
    bool changed = true;
};

class Module {
public:
    Module() = delete;
    explicit Module(std::string_view def_name) : def_name(def_name), inst_name(def_name) {}
    explicit Module(std::string_view def_name, std::string_view inst_name)
        : def_name(def_name), inst_name(inst_name) {}
    virtual void init(Scheduler *) {}
    virtual void comb(Scheduler *) {}
    virtual void always_comb(Scheduler *) {}
    virtual void nba(Scheduler *) {}
    virtual void final(Scheduler *) {}

    std::string_view def_name;
    std::string_view inst_name;

    Module *parent = nullptr;

    [[nodiscard]] std::string hierarchy_name() const;

    // active region
    void active();

protected:
    std::vector<CombProcess *> comb_processes_;

private:
    std::shared_ptr<marl::DAG<void>> comb_dag_;
};
}  // namespace xsim::runtime

#endif  // XSIM_MODULE_HH
