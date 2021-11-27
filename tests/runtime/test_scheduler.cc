#include <chrono>

#include "../../src/runtime/macro.hh"
#include "../../src/runtime/module.hh"
#include "../../src/runtime/scheduler.hh"
#include "../../src/runtime/system_task.hh"
#include "gtest/gtest.h"

using namespace xsim::runtime;
using namespace logic::literals;

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
            auto next_time = ScheduledTimeslot(scheduler->sim_time + 2, init_ptr);
            scheduler->schedule_delay(next_time);
            init_ptr->cond.signal();
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

class MultiInitModuleDelay : public Module {
public:
    MultiInitModuleDelay() : Module("multi_init_delay_test") {}
    bool sequence = false;
    void init(Scheduler *scheduler) override {
        {
            auto init_ptr = scheduler->create_init_process();
            init_ptr->func = [init_ptr, scheduler, this]() {
                // #5 delay
                auto next_time = ScheduledTimeslot(scheduler->sim_time + 5, init_ptr);
                scheduler->schedule_delay(next_time);
                // have to signal not running before unlock the main thread
                init_ptr->cond.signal();
                init_ptr->delay.wait();
                EXPECT_TRUE(sequence);
                // done with this init
                init_ptr->cond.signal();
                init_ptr->finished = true;
            };
            Scheduler::schedule_init(init_ptr);
        }
        {
            auto init_ptr = scheduler->create_init_process();
            init_ptr->func = [init_ptr, scheduler, this]() {
                // #2 delay
                auto next_time = ScheduledTimeslot(scheduler->sim_time + 2, init_ptr);
                scheduler->schedule_delay(next_time);
                // have to signal not running before unlock the main thread
                init_ptr->cond.signal();
                init_ptr->delay.wait();
                sequence = true;
                // done with this init
                init_ptr->cond.signal();
                init_ptr->finished = true;
            };
            Scheduler::schedule_init(init_ptr);
        }
    }
};

TEST(runtime, multi_init_delay) {  // NOLINT
    // multiple times to ensure the ordering
    for (auto i = 0; i < 100; i++) {
        Scheduler scheduler;
        MultiInitModuleDelay m;
        scheduler.run(&m);
        EXPECT_EQ(scheduler.sim_time, 5);
    }
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

TEST(runtime, finish) {  // NOLINT
    Scheduler scheduler;
    FinishModule m;
    scheduler.run(&m);
}

class FinalModule : public Module {
public:
    FinalModule() : Module("final_test") {}
    void final(Scheduler *scheduler) override {
        auto final_ptr = scheduler->create_final_process();
        final_ptr->func = [this]() { display(this, "PASS"); };
        Scheduler::schedule_final(final_ptr);
    }
};

TEST(runtime, final) {  // NOLINT
    Scheduler scheduler;
    FinalModule m;
    testing::internal::CaptureStdout();
    scheduler.run(&m);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("PASS"), std::string::npos);
}

class CombModuleOneBlock : public Module {
public:
    CombModuleOneBlock() : Module("comb_one_block") {}

    /*
     * module top;
     * logic [3:0] a, b;
     *
     * initial begin
     *     a = 4;
     *     #5;
     *     $display("b is %0d", b);
     * end
     *
     * always_comb begin
     *     b = a;
     * end
     * endmodule
     */

    logic_t<3, 0> a;
    logic::logic<3, 0> b;

    void init(Scheduler *scheduler) override {
        auto init_ptr = scheduler->create_init_process();
        init_ptr->func = [init_ptr, scheduler, this]() {
            a = 4_logic;
            auto next_time = ScheduledTimeslot(scheduler->sim_time + 5, init_ptr);
            scheduler->schedule_delay(next_time);
            // have to signal not running before unlock the main thread
            init_ptr->running = false;
            init_ptr->cond.signal();
            init_ptr->delay.wait();
            // print out b value
            display(this, "b is %0d", b);
            // done with this init
            init_ptr->finished = true;
            init_ptr->cond.signal();
        };
        Scheduler::schedule_init(init_ptr);
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->input_changed = [this]() {
            bool res = false;
            XSIM_CHECK_CHANGED(this, a, res);
            return res;
        };
        always->func = [scheduler, this, always] {
            b = a;  // NOLINT
        };
        comb_processes_.emplace_back(always);
    }
};

TEST(runtime, comb_one_block) {  // NOLINT
    Scheduler scheduler;
    CombModuleOneBlock m;
    testing::internal::CaptureStdout();
    scheduler.run(&m);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "b is 4\n");
}

class CombModuleMultipleTrigger : public Module {
public:
    CombModuleMultipleTrigger() : Module("comb_multiple_trigger") {}

    /*
     * module top;
     *
     * logic [3:0] a, b, c;
     *
     * always @(*) begin
     *     c = a;
     *     #5
     *     c = b + 1;
     * end
     *
     * initial begin
     *     for (int i = 0; i < 4; i++) begin
     *         a = i;
     *         #2 b = i;
     *         #2;
     *     end
     * end
     * endmodule
     */

    logic_t<3, 0> a;
    logic_t<3, 0> b;
    logic::logic<3, 0> c;

    void init(Scheduler *scheduler) override {
        auto init_ptr = scheduler->create_init_process();
        init_ptr->func = [init_ptr, scheduler, this]() {
            for (auto i = 0u; i < 4; i++) {
                a = logic::logic<3, 0>(i);
                auto next_time = ScheduledTimeslot(scheduler->sim_time + 2, init_ptr);
                // printf("schedule first a change @ %ld -> %ld\n", scheduler->sim_time,
                //        next_time.time);
                scheduler->schedule_delay(next_time);
                // have to signal not running before unlock the main thread
                std::atomic_thread_fence(std::memory_order_seq_cst);
                std::atomic_thread_fence(std::memory_order_seq_cst);
                init_ptr->cond.signal();
                init_ptr->delay.wait();

                b = logic::logic<3, 0>(i);

                next_time = ScheduledTimeslot(scheduler->sim_time + 2, init_ptr);
                // printf("schedule second a change @ %ld -> %ld\n", scheduler->sim_time,
                //        next_time.time);
                scheduler->schedule_delay(next_time);
                init_ptr->cond.signal();
                init_ptr->delay.wait();
            }
            // done with this init
            init_ptr->cond.signal();
            init_ptr->finished = true;
        };
        Scheduler::schedule_init(init_ptr);
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->input_changed = [this]() {
            bool res = this->a.changed || this->b.changed;
            // printf("input changed: %d\n", res);
            return res;
        };
        always->func = [scheduler, this, always] {
            c = a;  // NOLINT
            printf("c = %0ld @ %ld\n", c.to_uint64(), scheduler->sim_time);

            auto next_time = ScheduledTimeslot(scheduler->sim_time + 5, always);
            scheduler->schedule_delay(next_time);
            // have to signal not running before unlock the main thread
            always->cond.signal();
            always->delay.wait();

            c = b + 1_logic;

            printf("c = %0ld @ %ld\n", c.to_uint64(), scheduler->sim_time);
        };
        always->cancel_changed = [this]() {
            a.changed = false;
            b.changed = false;
        };
        comb_processes_.emplace_back(always);
    }
};

TEST(runtime, comb_multi_trigger) {  // NOLINT
    for (auto i = 0; i < 100; i++) {
        Scheduler scheduler;
        CombModuleMultipleTrigger m;
        testing::internal::CaptureStdout();
        scheduler.run(&m);
        std::string output = testing::internal::GetCapturedStdout();
        // printf("%s", output.c_str());
        EXPECT_NE(output.find("c = 4 @ 17"), std::string::npos);
    }
}