#ifndef FSIM_SYSTEM_TASK_HH
#define FSIM_SYSTEM_TASK_HH

#include <functional>
#include <iostream>

#include "logic/logic.hh"
#include "scheduler.hh"

namespace fsim::runtime {

class Module;
class Scheduler;

void print_assert_error(std::string_view name, std::string_view loc);

struct cout_lock {
    cout_lock();
    ~cout_lock();
};

template <typename T>
bool assert_(const T &v, std::string_view name, std::string_view loc) {
    auto bool_ = static_cast<bool>(v);
    if (!bool_) {
        print_assert_error(name, loc);
    }
    return bool_;
}

template <typename T>
void assert_(const T &v, std::string_view name, std::string_view loc,
             const std::function<void()> &ok, const std::function<void()> &bad) {
    auto bool_ = assert_(v, name, loc);
    if (bool_) {
        ok();
    } else {
        bad();
    }
}

std::string preprocess_display_fmt(const Module *module, std::string_view format);
std::pair<std::string_view, uint64_t> preprocess_display_fmt(std::string_view format);

// induction case
template <typename T, typename... Args>
uint64_t display_(const Module *m, std::string_view format, T arg, Args... args) {
    auto start_pos = display_(m, format, arg);
    return display_(m, format.substr(start_pos), args...);
}

// base case
template <typename T>
uint64_t display_(const Module *, std::string_view format, const T &arg) {
    auto [fmt, start_pos] = preprocess_display_fmt(format);
    if (!fmt.empty()) {
        if constexpr (std::is_same_v<T, const char *> || std::is_arithmetic_v<T>) {
            std::cout << arg;
        } else {
            std::cout << arg.str(fmt);
        }
    }
    return start_pos;
}

template <typename... Args>
void display(const Module *module, std::string_view format, Args... args) {
    // only display the format for now
    cout_lock lock;
    auto fmt = preprocess_display_fmt(module, format);
    display_(module, fmt, args...);
    std::cout << std::endl;
}

void display(const Module *module, std::string_view format);

template <typename T>
inline void finish(Scheduler *scheduler, T code, std::string_view loc) {
    int finish_code;
    if constexpr (std::is_arithmetic_v<T>) {
        finish_code = code;
    } else {
        finish_code = code.to_uint64();
    }
    scheduler->schedule_finish(finish_code, loc);
}

[[maybe_unused]] inline void finish(Scheduler *scheduler, std::string_view loc) {
    scheduler->schedule_finish(0, loc);
}

[[maybe_unused]] inline uint64_t time(Scheduler *scheduler) { return scheduler->sim_time; }

}  // namespace fsim::runtime

#endif  // FSIM_SYSTEM_TASK_HH
