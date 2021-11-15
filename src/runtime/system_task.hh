#ifndef XSIM_SYSTEM_TASK_HH
#define XSIM_SYSTEM_TASK_HH

#include <functional>
#include <iostream>

#include "logic/logic.hh"

namespace xsim::runtime {

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

template <typename ...Args>
void display(std::string_view format, Args ...args) {
    // only display the format for now
    std::cout << format << std::endl;
}

}  // namespace xsim::runtime

#endif  // XSIM_SYSTEM_TASK_HH
