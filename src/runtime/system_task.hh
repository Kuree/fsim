#ifndef XSIM_SYSTEM_TASK_HH
#define XSIM_SYSTEM_TASK_HH

#include <functional>
#include <iostream>

#include "logic/logic.hh"
#include "scheduler.hh"

namespace xsim::runtime {

class Module;

void print_assert_error(std::string_view name, std::string_view loc);

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

template <typename... Args>
void display(const Module *module, std::string_view format, Args...) {
    // only display the format for now
    auto fmt = preprocess_display_fmt(module, format);
    std::cout << fmt << std::endl;
}

template <typename T>
inline void finish(const Module *, T code = 0) {
    throw FinishException(code.to_uint64());
}

}  // namespace xsim::runtime

#endif  // XSIM_SYSTEM_TASK_HH
