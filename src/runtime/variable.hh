#ifndef FSIM_VARIABLE_HH
#define FSIM_VARIABLE_HH

#include <mutex>

#include "logic/logic.hh"

namespace fsim::runtime {
struct CombProcess;
struct FFProcess;
struct Process;

bool trigger_posedge(const logic::logic<0> &old, const logic::logic<0> &new_);
bool trigger_negedge(const logic::logic<0> &old, const logic::logic<0> &new_);

class TrackedVar {
public:
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

    void reset();

protected:
    void trigger_process();
    void update_edge_trigger(const logic::logic<0> &old, const logic::logic<0> &new_);
};

template <int msb = 0, int lsb = 0, bool signed_ = false>
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

    template <int op_msb, int op_lsb, bool op_signed_>
    logic::logic<size - 1, 0, signed_> &operator=(const logic::bit<op_msb, op_lsb, op_signed_> &v) {
        update_value(logic::logic(v));
        return *this;
    }

    logic_t<size - 1, 0, signed_> &operator=(const logic_t<msb, lsb, signed_> &v) {
        update_value(v);
        return *this;
    }

    template <typename T>
    requires(std::is_arithmetic_v<T>) logic_t &operator=(const T &v) {
        update_value(logic::logic<msb, lsb, signed_>(v));
        return *this;
    }

    logic_t(const logic_t<msb, lsb, signed_> &v) { update_value(v); }

    explicit logic_t(const logic::logic<msb, lsb, signed_> &v) { update_value(v); }

    logic_t() : TrackedVar(), logic::logic<msb, lsb, signed_>() {}

    template <int op_msb, int op_lsb, bool op_signed_>
    void update_value(const logic::logic<op_msb, op_lsb, op_signed_> &v) {
        if (!this->match(v)) {
            // dealing with edge triggering events
            if constexpr (size == 1) {
                // only allowed for size 1 signals
                auto const new_v = v[logic::util::min(op_msb, op_lsb)];
                update_edge_trigger(*this, new_v);
            }
            logic::logic<msb, lsb, signed_>::operator=(v);
            trigger_process();
        }
    }
};

template <int msb = 0, int lsb = 0, bool signed_ = false>
class bit_t : public logic::bit<msb, lsb, signed_>, public TrackedVar {
public:
    // t for tracking
    static auto constexpr size = logic::util::abs_diff(msb, lsb) + 1;
    // we intentionally hide the underling assign operator function
    // maybe try the type
    // TODO: change this into a virtual function and rely on slang's conversion operator
    //  to automatically convert types. thus disabling logic::logic's ability to change types
    template <int op_msb, int op_lsb, bool op_signed_>
    logic::bit<size - 1, 0, signed_> &operator=(const logic::bit<op_msb, op_lsb, op_signed_> &v) {
        update_value(v);
        return *this;
    }

    template <typename T>
    requires(std::is_arithmetic_v<T>) bit_t &operator=(const T &v) {
        update_value(logic::bit<msb, lsb, signed_>(v));
        return *this;
    }

    bit_t &operator=(const bit_t<msb, lsb, signed_> &v) {
        update_value(v);
        return *this;
    }

    bit_t(const bit_t<msb, lsb, signed_> &v) { update_value(v); }

    explicit bit_t(const logic::bit<msb, lsb, signed_> &v) { update_value(v); }

    bit_t() : TrackedVar(), logic::bit<msb, lsb, signed_>() {}

    template <int op_msb, int op_lsb, bool op_signed_>
    void update_value(const logic::bit<op_msb, op_lsb, op_signed_> &v) {
        if (!this->match(v)) {
            // dealing with edge triggering events
            if constexpr (size == 1) {
                // only allowed for size 1 signals
                auto const new_v = v[logic::util::min(op_msb, op_lsb)];
                update_edge_trigger(*this, new_v);
            }
            logic::bit<msb, lsb, signed_>::operator=(v);
            trigger_process();
        }
    }
};
}  // namespace fsim::runtime

#endif  // FSIM_VARIABLE_HH
