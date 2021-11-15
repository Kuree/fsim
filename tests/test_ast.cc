#include "../src/ast.hh"
#include "gtest/gtest.h"
#include "slang/syntax/SyntaxTree.h"

using namespace xsim;
using namespace slang;

TEST(ast, instance) {  // NOLINT
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

TEST(ast, module_complexity) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
    logic a, b, c, d;
    assign a = b;
    always_comb begin
        if (c) b = c;
        else b = c;
    end
    always_comb begin
        for (int i = 0; i < 4; i++) begin
            d = 1;
        end
    end
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    ModuleComplexityVisitor v;
    compilation.getRoot().visit(v);
    EXPECT_EQ(v.complexity, 1 + 2 + 2 + 2);
}

TEST(ast, var_dep_chained) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
    logic a, b, c, d;
    assign a = b;
    assign b = c;
    assign c = d;
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    DependencyAnalysisVisitor v;
    compilation.getRoot().visit(v);
    EXPECT_TRUE(v.error.empty());
    EXPECT_EQ(v.graph->nodes.size(), 4);
    auto const *n = v.graph->get_node("d");
    EXPECT_TRUE(n);
    for (auto i = 0; i < 3; i++) {
        EXPECT_EQ(n->edges_to.size(), 1);
        n = *n->edges_to.begin();
    }
    EXPECT_EQ(n->symbol.name, "a");
}

TEST(ast, var_dep_mult_left) {  // NOLINT
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
    auto *n = v.graph->get_node("a");
    EXPECT_TRUE(n);
    EXPECT_EQ(n->edges_from.size(), 2);
}

TEST(ast, procedural_dep) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic a, b, c, d, e, f, g;
always_comb begin
    if (a) b = c;
    else b = d;
end
always_latch begin
    case (a)
        0: e = f;
    endcase
end
always @(*)
    f = g;
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    DependencyAnalysisVisitor v;
    compilation.getRoot().visit(v);
    auto *n = v.graph->get_node(".blk0");
    EXPECT_TRUE(n);
    EXPECT_EQ(n->edges_to.size(), 1);
    EXPECT_EQ(n->edges_from.size(), 3);

    n = v.graph->get_node(".blk1");
    EXPECT_TRUE(n);
    EXPECT_EQ(n->edges_to.size(), 1);
    EXPECT_EQ(n->edges_from.size(), 2);

    n = v.graph->get_node(".blk2");
    EXPECT_TRUE(n);
    EXPECT_EQ(n->edges_to.size(), 1);
    EXPECT_EQ(n->edges_from.size(), 1);
}