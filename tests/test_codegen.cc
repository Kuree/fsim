#include "../src/builder/builder.hh"
#include "gtest/gtest.h"
#include "slang/compilation/Compilation.h"
#include "slang/syntax/SyntaxTree.h"
#include "util.hh"

using namespace fsim;
using namespace slang;

// -O3
constexpr uint8_t optimization_level = 3;

TEST(code, declaration) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic [3:0] a, b;
logic c;
initial begin
    logic d;
    $display("PASS", a, b, c);
end
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("PASS"), std::string::npos);
}

TEST(code, single_assign) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
// use integer to test out uncommon code path
integer a;
integer b;
wire  result;

assign result = (a != b);

initial begin
    a = 1;
    b = 2;
    #0;
    $display("result = %0d\n", result);
end
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("result = 1"), std::string::npos);
}

TEST(code, if_stmt) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic [3:0] a;
initial begin
    a = 3;
    if (a == 3) begin
        $display("PASS");
    end else begin
        $display("FAIL");
    end
end
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    // TODO: enhance this test case once display is working
    EXPECT_NE(output.find("PASS"), std::string::npos);
}

TEST(code, delay) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
initial begin
    #42 $display("PASS");
    $finish(1);
end
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("$finish(1)"), std::string::npos);
    EXPECT_NE(output.find("PASS"), std::string::npos);
}

TEST(code, multi_init) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
initial begin
    #42 $display("PASS");
    $finish(1);
end

initial begin
    #5 $display("TESTING");
end
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("TESTING\nPASS\n$finish(1) called at 42"), std::string::npos);
    printf("%s\n", output.c_str());
}

TEST(code, final) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
final begin
    $display("PASS");
end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("PASS"), std::string::npos);
}

TEST(code, always_assign) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic [3:0] a, b, c;
always_comb begin
    a = b + 1;
end
assign c = b + 2;

initial begin
    b = 1;
    #1;
    $display("a=%0d c=%0d", a, c);
    #1;
    b = 2;
    #1;
    $display("a=%0d c=%0d", a, c);
end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("a=2 c=3"), std::string::npos);
    EXPECT_NE(output.find("a=3 c=4"), std::string::npos);
}

TEST(code, always_ff_single_trigger) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic [3:0] a, b;
logic clk;

always_ff @(posedge clk) begin
    b = a;
end

initial begin
    clk = 0;
    a = 0;
    #1;
    clk = 1;
    a = 1;
    #1;
    clk = 0;
    a = 2;
    $display("a=%0d b=%0d", a, b);
    #1
    clk = 1;
    a = 3;
    #1
    $display("a=%0d b=%0d", a, b);
end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("a=2 b=1\na=3 b=3"), std::string::npos);
}

TEST(code, always_ff_nba) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic [3:0] a, b;
logic clk;

always_ff @(posedge clk) begin
    b <= a;
end

initial begin
    clk = 0;
    a = 0;
    #1;
    clk = 1;
    a = 1;
    #1;
    clk = 0;
    a = 2;
    $display("a=%0d b=%0d", a, b);
    #1
    clk = 1;
    a = 3;
    #1
    $display("a=%0d b=%0d", a, b);
end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("a=2 b=1\na=3 b=3"), std::string::npos);
}

TEST(code, loop) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
initial begin
    logic [3:0] sum = 0;
    for (int i = 0; i < 4; i++) begin
        sum += i;
    end
    $display("sum=%0d", sum);
end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("sum=6\n"), std::string::npos);
}

TEST(code, child_instance) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module child (
    input logic clk,
    input logic[5:0] in,
    output logic[5:0] out);

always_ff @(posedge clk)
    out <= in;
endmodule
module m;
logic clk;
logic[5:0] in, out, a, b;

child inst (.*);

assign in = a + 1;
assign b = out + 1;
initial begin
    clk = 0;
    a = 1;
    #1;
    clk = 1;
    #1;
    $display("b=%0d", b);
end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("b=3\n"), std::string::npos);
}

TEST(code, repeat) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic[5:0] a;
initial begin
    a = 2;
    repeat (2) $display("2");
    repeat (a) $display("4");
    forever begin
        $display("8");
        #2;
    end
end
initial #2 $finish;
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("2\n2\n4\n4\n"), std::string::npos);
}

TEST(code, slice) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic[5:0] a;
logic b;
initial begin
    a = 6'b111111;
    b = a[3];
    if (b == 1'b1) begin
        $display("PASS");
    end
end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("PASS"), std::string::npos);
}

TEST(code, concat) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic[5:0] a;
logic b;
logic[6:0] c;
initial begin
    a = 6'b111111;
    b = 1'b0;
    c = {a, b};
    $display("0c=%0b", c);
    c = 7'b1010101;
    {b, a} = c;
    $display("1a=%0b 1b=%0b", a, b);
    {b, a} <= 7'b0101010;
    $display("2a=%0b 2b=%0b", a, b);
    #0;
    $display("3a=%0b 3b=%0b", a, b);
end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("0c=1111110"), std::string::npos);
    EXPECT_NE(output.find("1a=010101 1b=1"), std::string::npos);
    EXPECT_NE(output.find("2a=010101 2b=1"), std::string::npos);
    EXPECT_NE(output.find("3a=101010 3b=0"), std::string::npos);
}

TEST(code, escape) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
initial begin
    $display("A\n");
    $display("B\tB");
end
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("A\n\n"), std::string::npos);
    EXPECT_NE(output.find("B\tB\n"), std::string::npos);
}

TEST(code, single_event) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module top;
logic clk;

initial begin
clk = 0;
#5 clk = 1;
#5 clk = 0;
#5 clk = 1;
end

initial begin
    @(posedge clk);
    $display("time is %t", $time);
    @(negedge clk);
    $display("time is %t", $time);
    @(clk)
    $display("time is %t", $time);
end

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("time is 5\ntime is 10\ntime is 15\n"), std::string::npos);
}

TEST(code, delay_rhs) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module top;
logic [31:0] a, b;

initial begin
    #0;
    a = #2 b;
end

initial begin
    b = 1;
    #1 b = 2;
    #2;
    $display("a = %0d", a);
end

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("a = 1\n"), std::string::npos);
}

TEST(code, dpi) {  // NOLINT
    auto dpi_c = R"(
int add(int a, int b) {
    return a + b;
}
)";

    // build the shared library first
    constexpr auto dpi_c_lib = "fsim_dir/dpi_c.so";
    build_c_shared_lib(dpi_c, dpi_c_lib);

    auto tree = SyntaxTree::fromText(R"(
module top;
import "DPI-C" function int add(int a, int b);
logic [4:0] a, b, c;

initial begin
    a = 1;
    b = 2;
    c = add(a, b);
    $display("c = %0d", c);
end

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.sv_libs.emplace_back(dpi_c_lib);

    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("c = 3\n"), std::string::npos);
}

TEST(code, array) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module top;
logic [31:0] a[3:0];
logic [1:0] b;
logic clk;

always_ff @(posedge clk)
  a[b] <= 1;

initial clk = 0;
always clk = #1 ~clk;

initial begin
    b = 1;
    #4;
    $display("a[1] = %0d", a[1]);
    $finish();
end

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;

    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("a[1] = 1"), std::string::npos);
}

TEST(code, vpi) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module top;
initial begin
    $finish();
end
endmodule
)");

    auto vpi_c = R"(
#include <stdio.h>

#include "vpi_user.h"

void load() {
    s_vpi_vlog_info info;
    vpi_get_vlog_info(&info);
    printf("argc: %d\n", info.argc);
    printf("argv[0]: %s\n", info.argv[0]);
}
void (*vlog_startup_routines[])() = {load, NULL};
)";

    // build the shared library first
    constexpr auto vpi_c_lib = "fsim_dir/vpi_c.so";
    build_c_shared_lib(vpi_c, vpi_c_lib);

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;
    options.vpi_libs.emplace_back(vpi_c_lib);

    options.optimization_level = 0;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("argc: 1\n"
                          "argv[0]: ./fsim.out"),
              std::string::npos);
}

TEST(code, function) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
function int add1(int a, int b);
    add1 = a + b;
endfunction

module top;

function int add2(int a, int b);
    return a + b;
endfunction

initial begin
    $display("add1: %0d", add1(1, 2));
    $display("add2: %0d", add2(3, 4));
end

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;

    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("add1: 3\n"
                          "add2: 7"),
              std::string::npos);
}

TEST(code, task) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
task task1(int a, int b);
    a = a + b;
    #40;
endtask

module top;

task task2();
    #2;
endtask

initial begin
    task1(1, 2);
    task2();
    $finish(0);
end

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;

    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("$finish(0) called at 42"), std::string::npos);
}

TEST(code, fork) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module top;

int a, b;
initial begin
   fork
       begin
           #4;
           a = 1;
           $display("%t a = %0d", $time, a);
       end
       begin
           #2;
           b = 2;
           $display("%t b = %0d", $time, b);
       end
   join
end

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    BuildOptions options;

    options.optimization_level = optimization_level;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("2 b = 2\n"
                          "4 a = 1"),
              std::string::npos);
}
