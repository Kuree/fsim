#ifndef FSIM_SYSTEM_TASK_HH
#define FSIM_SYSTEM_TASK_HH

#include <cstdio>
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
std::pair<std::string, uint64_t> sformat_(const Module *m, std::string_view format, T arg,
                                          Args... args) {
    auto start_pos = display_(m, format, arg);
    return sformat_(m, format.substr(start_pos), args...);
}

// base case
template <typename T>
std::pair<std::string, uint64_t> sformat_(const Module *module, std::string_view format,
                                          const T &arg) {
    auto processed_format = preprocess_display_fmt(module, format);
    auto [fmt, start_pos] = preprocess_display_fmt(format);
    std::string res;
    if (!fmt.empty()) {
        if constexpr (std::is_same_v<T, const char *> || std::is_arithmetic_v<T>) {
            res = arg;
        } else {
            res = arg.str(fmt);
        }
    }

    return {res, start_pos};
}

template <typename... Args>
void display(const Module *module, std::string_view format, Args... args) {
    auto s = sformat_(module, format, args...);
    cout_lock lock;
    std::cout << s << std::endl;
}

template <typename... Args>
void write(const Module *module, std::string_view format, Args... args) {
    auto s = sformat_(module, format, args...);
    cout_lock lock;
    std::cout << s;
}

void display(const Module *module, std::string_view format);
void write(const Module *module, std::string_view format);

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

int32_t fopen(std::string_view filename, std::string_view mode);
template <typename T>
int32_t fopen(std::string_view filename, T mode) {
    auto mod_str = mode.str("%s");
    return fopen(filename, mod_str);
}

void fclose(int32_t fd);

void fwrite_(int32_t fd, std::string_view str, bool new_line);
template <typename... Args>
void fwrite(const Module *module, int32_t fd, std::string_view format, Args... args) {
    // only display the format for now
    cout_lock lock;
    auto fmt = preprocess_display_fmt(module, format);
    auto s = sformat_(module, fmt, args...);
    fwrite_(fd, s);
}

void fdisplay_(int32_t fd, std::string_view str);
template <typename... Args>
void fdisplay(const Module *module, int32_t fd, std::string_view format, Args... args) {
    // only display the format for now
    cout_lock lock;
    auto fmt = preprocess_display_fmt(module, format);
    auto s = sformat_(module, fmt, args...);
    fdisplay_(fd, s);
}

}  // namespace fsim::runtime

#endif  // FSIM_SYSTEM_TASK_HH
