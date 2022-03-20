#include "../src/builder/builder.hh"
#include "gtest/gtest.h"
#include "slang/compilation/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

using namespace fsim;
using namespace slang;

// -O3
constexpr uint8_t optimization_level = 3;

TEST(builder, single_module) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
initial begin
    $display("HELLO WORLD");
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
    EXPECT_NE(output.find("HELLO WORLD"), std::string::npos);
}
