#include "../../src/runtime/module.hh"
#include "../../src/runtime/scheduler.hh"
#include "../../src/runtime/system_task.hh"
#include "gtest/gtest.h"
#include "slang/syntax/SyntaxTree.h"

using namespace xsim::runtime;
using namespace slang;

class InitModuleNoDelay: public Module {
public:
    InitModuleNoDelay(): Module("init_no_delay_test") {}
    void init(Scheduler *scheduler) override {
        auto init_ptr = std::make_shared<InitialProcess>();
        init_ptr->func = [init_ptr]() {
            display("HELLO WORLD");
            // done with this init
            init_ptr->finished = true;
            init_ptr->cond.signal();
        };
        scheduler->schedule_init(init_ptr);
    }
};

TEST(runtime, init_no_delay) {  // NOLINT
    Scheduler scheduler;
    InitModuleNoDelay m;
    testing::internal::CaptureStdout();
    scheduler.run(&m);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("HELLO WORLD"), std::string::npos);
}