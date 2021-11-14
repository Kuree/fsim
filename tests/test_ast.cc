#include "../src/ast.hh"
#include "gtest/gtest.h"
#include "slang/syntax/SyntaxTree.h"

using namespace xsim;
using namespace slang;

TEST(ast, instance) {   // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m1;
endmodule
module m2;
endmodule
module m3;
m1 m1_();
endmodule
module m;
    m1 m1_();
    m2 m2_();
    m3 m3_();
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    ModuleDefinitionVisitor vis;
    compilation.getRoot().visit(vis);
    EXPECT_EQ(vis.modules.size(), 4);
}

TEST(ast, var_dep_mult_left) {    // NOLINT
    auto tree = SyntaxTree::fromText(R"(

module m;
    logic a, b, c, d;
    assign {a, b} = {c, d};
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    DependencyAnalysisVisitor v;
    compilation.getRoot().visit(v);
    EXPECT_TRUE(v.error.empty());
    auto *n = v.graph->node_mapping.at("a");
    EXPECT_EQ(n->edges_from.size(), 2);
}