#include "../../src/runtime/macro.hh"
#include "../../src/runtime/module.hh"
#include "../../src/runtime/scheduler.hh"
#include "../../src/runtime/system_task.hh"
#include "../../src/runtime/variable.hh"
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
        init_processes_.emplace_back(init_ptr);
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
        init_processes_.emplace_back(init_ptr);
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
            init_processes_.emplace_back(init_ptr);
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
            init_processes_.emplace_back(init_ptr);
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
        init_processes_.emplace_back(init_ptr);
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
        init_processes_.emplace_back(init_ptr);
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->func = [this, always] {
            b = a;  // NOLINT
            END_PROCESS(always);
        };
        comb_processes_.emplace_back(always);
        a.comb_processes.emplace_back(always);
    }
};

TEST(runtime, comb_one_block) {  // NOLINT
    for (auto i = 0; i < 1; i++) {
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
        init_processes_.emplace_back(init_ptr);
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->func = [scheduler, this, always] {
            c = a;  // NOLINT
            display(this, "c = %0d @ %d", c, scheduler->sim_time);

            SCHEDULE_DELAY(always, 5, scheduler, n);

            c = b + 1_logic;

            display(this, "c = %0d @ %d", c, scheduler->sim_time);
            END_PROCESS(always);
        };
        comb_processes_.emplace_back(always);
        a.comb_processes.emplace_back(always);
        b.comb_processes.emplace_back(always);
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
        process->func = [this, process]() {
            a = 1_logic;
            END_PROCESS(process);
        };

        ff_process_.emplace_back(process);
        clk.track_edge = true;
        clk.ff_posedge_processes.emplace_back(process);
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->func = [this, always] {
            b = a;  // NOLINT
            END_PROCESS(always);
        };
        comb_processes_.emplace_back(always);
        a.comb_processes.emplace_back(always);
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
        init_processes_.emplace_back(init_ptr);
    }
};

TEST(runtime, ff_blocking) {  // NOLINT
    for (auto i = 0; i < 100; i++) {
        Scheduler scheduler;
        FFBlockingAssignment m;
        testing::internal::CaptureStdout();
        scheduler.run(&m);
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_NE(output.find("b is 1"), std::string::npos);
    }
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
        process->func = [this, process]() {
            process->running = true;
            process->finished = false;
            marl::schedule([this, process]() {
                SCHEDULE_NBA(a, 1_logic, process);
                END_PROCESS(process);
            });
        };

        ff_process_.emplace_back(process);
        clk.track_edge = true;
        clk.ff_posedge_processes.emplace_back(process);
    }

    void comb(Scheduler *scheduler) override {
        auto *always = scheduler->create_comb_process();
        always->func = [this, always] {
            b = a;  // NOLINT
            END_PROCESS(always);
        };
        comb_processes_.emplace_back(always);
        a.comb_processes.emplace_back(always);
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
        init_processes_.emplace_back(init_ptr);
    }
};

TEST(runtime, ff_nba) {  // NOLINT
    for (auto i = 0; i < 100; i++) {
        Scheduler scheduler;
        FFNonBlockingAssignment m;
        testing::internal::CaptureStdout();
        scheduler.run(&m);
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_NE(output.find("b is 1\n"), std::string::npos);
    }
}

class ChildInstanceTest : public Module {
public:
    ChildInstanceTest() : Module("child") {}
    /*
     * module child (input logic clk,
     *               input logic[3:0] in,
     *               output logic[3:0] out);
     * always_ff @(posedge clk) out <= in;
     * endmodule
     */
    logic_t<0, 0> clk;
    logic_t<3, 0> in, out;

    void ff(Scheduler *scheduler) override {
        auto process = scheduler->create_ff_process();
        process->func = [this, process]() {
            SCHEDULE_NBA(out, in, process);
            END_PROCESS(process);
        };

        ff_process_.emplace_back(process);
        clk.track_edge = true;
        clk.ff_posedge_processes.emplace_back(process);
    }
};

class ChildInstanceTop : public Module {
public:
    ChildInstanceTop() : Module("top"), inst(std::make_shared<ChildInstanceTest>()) {
        child_instances_.emplace_back(inst.get());
    }

    /*
     *
     * module top;
     * logic[3:0] in, out, a;
     * logic clk;
     * child inst (.*);
     *
     * assign a = out;
     *
     * always_ff @(posedge clk)
     *     a <= 1;
     *
     * endmodule
     */

    logic_t<3, 0> out, in;
    logic::logic<3, 0> a;
    logic_t<0, 0> clk;

    std::shared_ptr<ChildInstanceTest> inst;

    void comb(Scheduler *scheduler) override {
        {
            auto *always = scheduler->create_comb_process();
            always->func = [this, always] {
                a = out;  // NOLINT
                END_PROCESS(always);
            };

            comb_processes_.emplace_back(always);
            out.comb_processes.emplace_back(always);
        }

        // a comb process for child instance input
        {
            auto *always = scheduler->create_comb_process();
            always->func = [this, always] {
                this->inst->in = in;
                this->inst->clk = clk;
                END_PROCESS(always);
            };
            comb_processes_.emplace_back(always);
            in.comb_processes.emplace_back(always);
            clk.comb_processes.emplace_back(always);
        }

        // a comb process for child instance output
        {
            auto *always = scheduler->create_comb_process();
            always->func = [this, always] {
                this->out = this->inst->out;
                END_PROCESS(always);
            };
            comb_processes_.emplace_back(always);
            this->inst->out.comb_processes.emplace_back(always);
        }

        Module::comb(scheduler);
    }

    void init(Scheduler *scheduler) override {
        auto init_ptr = scheduler->create_init_process();
        init_ptr->func = [init_ptr, scheduler, this]() {
            clk = 0_logic;
            in = 4_logic;
            SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
            clk = 1_logic;
            SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
            display(this, "a=%0d", a);

            // done with this init
            END_PROCESS(init_ptr);
        };
        Scheduler::schedule_init(init_ptr);
        init_processes_.emplace_back(init_ptr);

        Module::init(scheduler);
    }
};

TEST(runtime, inst) {  // NOLINT
    for (auto i = 0; i < 100; i++) {
        Scheduler scheduler;
        ChildInstanceTop m;
        testing::internal::CaptureStdout();
        scheduler.run(&m);
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_NE(output.find("a=4\n"), std::string::npos);
    }
}

class AlwaysGeneralPurpose : public Module {
public:
    /*
     * module m;
     *
     * always #5 clk = ~clk;
     * initial clk = 0;
     *
     * initial begin
     * #1;
     * for (int i = 0; i < 4; i++) begin
     *   $display("clk=%0d", clk);
     *   #5;
     * end
     * end
     *
     *
     * endmodule
     */

    AlwaysGeneralPurpose() : Module("always_general_purpose") {}

    void comb(Scheduler *scheduler) override {
        {
            auto *always = scheduler->create_comb_process();
            always->func = [this, always, scheduler] {
                while (true) {
                    SCHEDULE_DELAY(always, 5, scheduler, n);
                    clk = ~clk;
                }
                END_PROCESS(always);
            };
            comb_processes_.emplace_back(always);
            clk.comb_processes.emplace_back(always);
        }
    }

    void init(Scheduler *scheduler) override {
        {
            auto init_ptr = scheduler->create_init_process();
            init_ptr->func = [init_ptr, this]() {
                clk = 0_logic;
                // done with this init
                END_PROCESS(init_ptr);
            };
            Scheduler::schedule_init(init_ptr);
            init_processes_.emplace_back(init_ptr);
        }

        {
            auto init_ptr = scheduler->create_init_process();
            init_ptr->func = [init_ptr, scheduler, this]() {
                SCHEDULE_DELAY(init_ptr, 1, scheduler, n);
                for (auto i = 0; i < 4; i++) {
                    display(this, "clk=%0d", clk);
                    SCHEDULE_DELAY(init_ptr, 5, scheduler, n);
                }
                finish(scheduler, 0);
                // done with this init
                END_PROCESS(init_ptr);
            };
            Scheduler::schedule_init(init_ptr);
            init_processes_.emplace_back(init_ptr);
        }
    }

    logic_t<0> clk;
};

TEST(runtime, always_general_purpose) {  // NOLINT
    Scheduler scheduler;
    AlwaysGeneralPurpose m;
    testing::internal::CaptureStdout();
    scheduler.run(&m);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("clk=0\n"
                          "clk=1\n"
                          "clk=0\n"
                          "clk=1\n"
                          "$finish(0) called at 21"),
              std::string::npos);
    EXPECT_EQ(scheduler.sim_time, 21);
}

class Forever : public Module {
public:
    /*
     * module m;
     * initial begin
     * forever begin
     *     $display("42");
     *     #2;
     * end
     * end
     *
     * initial #2 $finish;
     *
     * endmodule
     */

    Forever() : Module("forever") {}

    void init(Scheduler *scheduler) override {
        {
            auto init_ptr = scheduler->create_init_process();
            init_ptr->func = [init_ptr, scheduler, this]() {
                while (true) {
                    display(this, "42");
                    SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
                }
                // done with this init
                END_PROCESS(init_ptr);
            };
            Scheduler::schedule_init(init_ptr);
            init_processes_.emplace_back(init_ptr);
        }

        {
            auto init_ptr = scheduler->create_init_process();
            init_ptr->func = [init_ptr, scheduler]() {
                SCHEDULE_DELAY(init_ptr, 2, scheduler, n);
                finish(scheduler, 0);
                // done with this init
                END_PROCESS(init_ptr);
            };
            Scheduler::schedule_init(init_ptr);
            init_processes_.emplace_back(init_ptr);
        }
    }
};

TEST(runtime, forever_loop) {  // NOLINT
    for (auto i = 0; i < 100; i++) {
        Scheduler scheduler;
        Forever m;
        testing::internal::CaptureStdout();
        scheduler.run(&m);
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_NE(output.find("42\n42\n"), std::string::npos);
        EXPECT_EQ(scheduler.sim_time, 2);
    }
}

class BothEdgeTimingControl : public Module {
    /*
     * module top;
     * logic clk;
     *
     * initial begin
     * clk = 0;
     * clk = #5 1;
     * clk = #5 0;
     * clk = #5 1;
     * end
     *
     * initial begin
     *     @(posedge clk);
     *     $display("time is %t", $time);
     *     @(negedge clk);
     *     $display("time is %t", $time);
     *     @(clk)
     *     $display("time is %t", $time);
     * end
     *
     * endmodule
     */
public:
    BothEdgeTimingControl() : Module("both_timing_control") {}

    logic_t<0> clk;

    void init(Scheduler *scheduler) override {
        {
            auto init_ptr = scheduler->create_init_process();
            init_ptr->func = [init_ptr, scheduler, this]() {
                clk = 0_bit;
                SCHEDULE_DELAY(init_ptr, 5, scheduler, n);
                clk = 1_bit;
                SCHEDULE_DELAY(init_ptr, 5, scheduler, n);
                clk = 0_bit;
                SCHEDULE_DELAY(init_ptr, 5, scheduler, n);
                clk = 1_bit;
                // done with this init
                END_PROCESS(init_ptr);
            };
            Scheduler::schedule_init(init_ptr);
            init_processes_.emplace_back(init_ptr);
        }

        {
            auto init_ptr = scheduler->create_init_process();
            init_ptr->func = [init_ptr, scheduler, this]() {
                SCHEDULE_EDGE(init_ptr, clk, Process::EdgeControlType::posedge);
                display(this, "time is %t", scheduler->sim_time);

                SCHEDULE_EDGE(init_ptr, clk, Process::EdgeControlType::negedge);
                display(this, "time is %t", scheduler->sim_time);

                SCHEDULE_EDGE(init_ptr, clk, Process::EdgeControlType::both);
                display(this, "time is %t", scheduler->sim_time);
                // done with this init
                END_PROCESS(init_ptr);
            };
            clk.track_edge = true;
            Scheduler::schedule_init(init_ptr);
            init_processes_.emplace_back(init_ptr);
            scheduler->add_tracked_var(&clk);
            scheduler->add_process_edge_control(init_ptr);
        }
    }
};

TEST(runtime, edge_control) {  // NOLINT
    for (auto i = 0; i < 420; i++) {
        Scheduler scheduler;
        BothEdgeTimingControl m;
        testing::internal::CaptureStdout();
        scheduler.run(&m);
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_NE(output.find("time is 5\ntime is 10\ntime is 15\n"), std::string::npos);
    }
}