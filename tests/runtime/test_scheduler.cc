#include "../../src/runtime/module.hh"
#include "../../src/runtime/scheduler.hh"
#include "../../src/runtime/system_task.hh"
#include "gtest/gtest.h"

using namespace xsim::runtime;

class InitModuleNoDelay : public Module {
public:
    InitModuleNoDelay() : Module("init_no_delay_test") {}
    void init(Scheduler *scheduler) override {
        auto init_ptr = scheduler->create_init_process();
        init_ptr->func = [init_ptr]() {
            display(nullptr, "HELLO WORLD");
            // done with this init
            init_ptr->finished = true;
            init_ptr->cond.signal();
        };
        Scheduler::schedule_init(init_ptr);
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
    void init(Scheduler *scheduler) override {
        auto init_ptr = scheduler->create_init_process();
        init_ptr->func = [init_ptr, scheduler, this]() {
            // #2 delay
            // switch to a new env variable
            init_ptr->cond.signal();
            auto next_time = ScheduledTimeslot(scheduler->sim_time + 2, init_ptr);
            scheduler->schedule_delay(next_time);
            init_ptr->cond.clear();
            init_ptr->delay.wait();
            // done with this init
            init_ptr->finished = true;
            init_ptr->cond.signal();
        };
        Scheduler::schedule_init(init_ptr);
    }
};

TEST(runtime, init_delay) {  // NOLINT
    Scheduler scheduler;
    InitModuleDelay m;
    scheduler.run(&m);
    EXPECT_EQ(scheduler.sim_time, 2);
}


class FinishModule : public Module {
public:
    FinishModule() : Module("finish_test") {}
    void init(Scheduler *scheduler) override {
        auto init_ptr = scheduler->create_init_process();
        init_ptr->func = [init_ptr, scheduler]() {
            finish(scheduler, 0);
            init_ptr->cond.signal();
        };
        Scheduler::schedule_init(init_ptr);
    }
};


TEST(runtime, finish) { // NOLINT
    Scheduler scheduler;
    FinishModule m;
    scheduler.run(&m);
}