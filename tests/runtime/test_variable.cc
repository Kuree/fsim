#include "../../src/runtime/module.hh"
#include "../../src/runtime/scheduler.hh"
#include "../../src/runtime/system_task.hh"
#include "../../src/runtime/variable.hh"
#include "../../src/runtime/macro.hh"
#include "gtest/gtest.h"


using namespace xsim::runtime;
using namespace logic::literals;

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

    bit_t<3, 0> a;
    logic::bit<3, 0> b;

    void init(Scheduler *scheduler) override {
        auto init_ptr = scheduler->create_init_process();
        init_ptr->func = [init_ptr, scheduler, this]() {
            a = 4;
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
        always->func = [this, always] {
            b = a;  // NOLINT
            END_PROCESS(always);
        };
        comb_processes_.emplace_back(always);
        a.comb_processes.emplace_back(always);
    }
};

TEST(two_state, tracking) {
    for (auto i = 0; i < 1; i++) {
        Scheduler scheduler;
        CombModuleOneBlock m;
        testing::internal::CaptureStdout();
        scheduler.run(&m);
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_EQ(output, "b is 4\n");
    }
}