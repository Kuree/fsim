#include "../src/builder.hh"
#include "gtest/gtest.h"
#include "slang/compilation/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

using namespace xsim;
using namespace slang;

TEST(codegen, declaration) {    // NOLINT
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
    options.debug_build = true;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    // TODO: enhance this test case once display is working
    EXPECT_NE(output.find("PASS"), std::string::npos);
}

TEST(codegen, if_stmt) {    // NOLINT
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
    options.debug_build = true;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    // TODO: enhance this test case once display is working
    EXPECT_NE(output.find("PASS"), std::string::npos);
}

TEST(codegen, delay) {    // NOLINT
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
    options.debug_build = true;
    options.run_after_build = true;
    Builder builder(options);
    testing::internal::CaptureStdout();
    builder.build(&compilation);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("$finish(1)"), std::string::npos);
    //EXPECT_NE(output.find("PASS"), std::string::npos);
}