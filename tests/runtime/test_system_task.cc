#include "../../src/runtime/module.hh"
#include "../../src/runtime/scheduler.hh"
#include "../../src/runtime/system_task.hh"
#include "gtest/gtest.h"
#include "logic/logic.hh"

using namespace xsim::runtime;

TEST(systask, display_m) {  // NOLINT
    Module m1("test", "test2");
    Module m2("test3", "test4");
    m2.parent = &m1;
    testing::internal::CaptureStdout();
    display(&m2, "%m %d");
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "test2.test4 %d\n");
    logic::logic<3, 0> a;
    testing::internal::CaptureStdout();
    display(&m2, "%b", a);
    output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "xxxx\n");
}