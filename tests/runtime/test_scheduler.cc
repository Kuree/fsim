#include "../../src/runtime/module.hh"
#include "../../src/runtime/scheduler.hh"
#include "../../src/runtime/system_task.hh"
#include "gtest/gtest.h"

using namespace xsim::runtime;

class InitModuleNoDelay : public Module {
public:
    InitModuleNoDelay() : Module("init_no_delay_test") {}
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

class InitModuleDelay : public Module {
public:
    InitModuleDelay() : Module("init_delay_test") {}
    marl::Event delay_cond = marl::Event(marl::Event::Mode::Manual);
    ScheduledTimeslot next_time = ScheduledTimeslot(0, delay_cond);
    void init(Scheduler *scheduler) override {
        auto init_ptr = std::make_shared<InitialProcess>();
        init_ptr->func = [init_ptr, scheduler, this]() {
            // #2 delay
            // switch to a new env variable
            init_ptr->cond.signal();
            next_time.time = scheduler->sim_time + 2;
            scheduler->schedule_delay(&next_time);
            init_ptr->cond.clear();
            delay_cond.wait();
            // done with this init
            init_ptr->finished = true;
            init_ptr->cond.signal();
        };
        scheduler->schedule_init(init_ptr);
    }
};

TEST(runtime, init_delay) {  // NOLINT
    Scheduler scheduler;
    InitModuleDelay m;
    scheduler.run(&m);
    EXPECT_EQ(scheduler.sim_time, 2);
}