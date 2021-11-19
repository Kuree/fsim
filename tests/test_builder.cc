#include "../src/builder.hh"
#include "gtest/gtest.h"
#include "slang/compilation/Compilation.h"
#include "slang/syntax/SyntaxTree.h"

using namespace xsim;
using namespace slang;

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
    options.debug_build = true;
    options.run_after_build = true;
    Builder builder(options);
    builder.build(&compilation);
}