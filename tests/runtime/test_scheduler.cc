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
            END_PROCESS(init_ptr);
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
        init_ptr->func = [init_ptr, scheduler]() {
            // #2 delay
            // switch to a new env variable
            SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
            // done with this init
            END_PROCESS(init_ptr);
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
                SCHEDULE_DELAY(init_ptr, 5, scheduler, n);
                EXPECT_TRUE(sequence);
                // done with this init
                END_PROCESS(init_ptr);
            };
            Scheduler::schedule_init(init_ptr);
        }
        {
            auto init_ptr = scheduler->create_init_process();
            init_ptr->func = [init_ptr, scheduler, this]() {
                // #2 delay
                SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
                sequence = true;
                // done with this init
                END_PROCESS(init_ptr);
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
            END_PROCESS(init_ptr);
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
            SCHEDULE_DELAY(init_ptr, 5, scheduler, n);
            // print out b value
            display(this, "b is %0d", b);
            // done with this init
            END_PROCESS(init_ptr);
        };
        Scheduler::schedule_init(init_ptr);
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->input_changed = [this]() { return a.changed; };
        always->func = [this] {
            b = a;  // NOLINT
        };
        always->cancel_changed = [this]() { a.changed = false; };
        comb_processes_.emplace_back(always);
    }
};

TEST(runtime, comb_one_block) {  // NOLINT
    for (auto i = 0; i < 100; i++) {
        Scheduler scheduler;
        CombModuleOneBlock m;
        testing::internal::CaptureStdout();
        scheduler.run(&m);
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_EQ(output, "b is 4\n");
    }
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
                SCHEDULE_DELAY(init_ptr, 2, scheduler, n);

                b = logic::logic<3, 0>(i);

                SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
            }
            // done with this init
            END_PROCESS(init_ptr);
        };
        Scheduler::schedule_init(init_ptr);
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->input_changed = [this]() {
            bool res = this->a.changed || this->b.changed;
            return res;
        };
        always->func = [scheduler, this, always] {
            c = a;  // NOLINT
            display(this, "c = %0d @ %d", c, scheduler->sim_time);

            SCHEDULE_DELAY(always, 5, scheduler, n);

            c = b + 1_logic;

            display(this, "c = %0d @ %d", c, scheduler->sim_time);
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
        EXPECT_NE(output.find("c = 4 @ 17"), std::string::npos);
    }
}

class FFBlockingAssignment : public Module {
public:
    FFBlockingAssignment() : Module("ff_blocking_assignment") {}

    /*
     * module top;
     * logic clk;
     * logic[3:0] a, b;
     *
     * always_comb
     *     b = a;
     *
     * always_ff @(posedge clk)
     *     a = 1;
     *
     * endmodule
     */

    logic_t<3, 0> a;
    logic::logic<3, 0> b;
    logic_t<0> clk;

    void ff(Scheduler *scheduler) override {
        auto process = scheduler->create_ff_process();
        process->func = [this, process, scheduler]() {
            process->running = true;
            process->finished = false;
            marl::schedule([this, process]() {
                a = 1_logic;
                END_PROCESS(process);
            });
        };

        process->should_trigger = [this]() { return clk.should_trigger_posedge; };
        process->cancel_changed = [this]() { clk.should_trigger_posedge = false; };

        ff_process_.emplace_back(process);
        clk.track_edge = true;
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->input_changed = [this]() {
            bool res = this->a.changed;
            return res;
        };
        always->func = [this] {
            b = a;  // NOLINT
        };
        always->cancel_changed = [this]() { a.changed = false; };
        comb_processes_.emplace_back(always);
    }

    void init(Scheduler *scheduler) override {
        auto init_ptr = scheduler->create_init_process();
        init_ptr->func = [init_ptr, scheduler, this]() {
            clk = 0_logic;
            SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
            clk = 1_logic;
            SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
            display(this, "b is %0d", b);

            // done with this init
            END_PROCESS(init_ptr);
        };
        Scheduler::schedule_init(init_ptr);
    }
};

TEST(runtime, ff_blocking) {  // NOLINT
    Scheduler scheduler;
    FFBlockingAssignment m;
    testing::internal::CaptureStdout();
    scheduler.run(&m);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("b is 1"), std::string::npos);
}

class FFNonBlockingAssignment : public Module {
public:
    FFNonBlockingAssignment() : Module("ff_nonblocking_assignment") {}

    /*
     * module top;
     * logic clk;
     * logic[3:0] a, b;
     *
     * always_comb
     *     b = a;
     *
     * always_ff @(posedge clk)
     *     a <= 1;
     *
     * endmodule
     */

    logic_t<3, 0> a;
    logic::logic<3, 0> b;
    logic_t<0> clk;

    void ff(Scheduler *scheduler) override {
        auto process = scheduler->create_ff_process();
        process->func = [this, process, scheduler]() {
            process->running = true;
            process->finished = false;
            marl::schedule([this, process]() {
                SCHEDULE_NBA(a, 1_logic, process);
                END_PROCESS(process);
            });
        };

        process->should_trigger = [this]() { return clk.should_trigger_posedge; };
        process->cancel_changed = [this]() { clk.should_trigger_posedge = false; };

        ff_process_.emplace_back(process);
        clk.track_edge = true;
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->input_changed = [this]() {
            bool res = this->a.changed;
            return res;
        };
        always->func = [this] {
            b = a;  // NOLINT
        };
        always->cancel_changed = [this]() { a.changed = false; };
        comb_processes_.emplace_back(always);
    }

    void init(Scheduler *scheduler) override {
        auto init_ptr = scheduler->create_init_process();
        init_ptr->func = [init_ptr, scheduler, this]() {
            clk = 0_logic;
            SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
            clk = 1_logic;
            SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
            display(this, "b is %0d", b);

            // done with this init
            END_PROCESS(init_ptr);
        };
        Scheduler::schedule_init(init_ptr);
    }
};

TEST(runtime, ff_nba) {  // NOLINT
    Scheduler scheduler;
    FFNonBlockingAssignment m;
    testing::internal::CaptureStdout();
    scheduler.run(&m);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("b is 1\n"), std::string::npos);
}