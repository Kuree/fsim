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
    EXPECT_EQ(v.graph->nodes.size(), 7);
    auto const *n = v.graph->get_node("d");
    EXPECT_TRUE(n);
    while (!n->edges_to.empty()) {
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
    auto const *n = v.graph->get_node("a");
    EXPECT_TRUE(n);
    n = *n->edges_from.begin();
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

TEST(ast, init_dep) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic a;
wire b = a;
wire c = b;
wire d = c;
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    DependencyAnalysisVisitor v;
    compilation.getRoot().visit(v);

    EXPECT_EQ(v.graph->nodes.size(), 4);
}

TEST(as, verilog_always_dep) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic a, b, c, d;
always @(posedge a, d) c = a; // d -> c  blk0
always @(*) a = b; // b -> a             blk1
always @(c) b = a; // c -> b             blk2
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    DependencyAnalysisVisitor v;
    compilation.getRoot().visit(v);

    auto *blk0 = v.graph->get_node(".blk0");
    auto *blk1 = v.graph->get_node(".blk1");
    auto *blk2 = v.graph->get_node(".blk2");
    auto const *a = v.graph->get_node("a");
    auto const *b = v.graph->get_node("b");
    auto const *c = v.graph->get_node("c");
    auto const *d = v.graph->get_node("d");
    // c <- blk0 <- d
    auto pos = std::find(blk0->edges_to.begin(), blk0->edges_to.end(), c);
    EXPECT_NE(pos, blk0->edges_to.end());
    pos = std::find(blk0->edges_from.begin(), blk0->edges_from.end(), d);
    EXPECT_NE(pos, blk0->edges_from.end());
    // a <- blk1 <- b
    pos = std::find(blk1->edges_to.begin(), blk1->edges_to.end(), a);
    EXPECT_NE(pos, blk1->edges_to.end());
    pos = std::find(blk1->edges_from.begin(), blk1->edges_from.end(), b);
    EXPECT_NE(pos, blk1->edges_from.end());
    // b <- blk2 <- c;
    pos = std::find(blk2->edges_to.begin(), blk2->edges_to.end(), b);
    EXPECT_NE(pos, blk2->edges_to.end());
    pos = std::find(blk2->edges_from.begin(), blk2->edges_from.end(), c);
    EXPECT_NE(pos, blk2->edges_from.end());
}

TEST(ast, procedure_extraction) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m2;
initial begin end
final begin end
endmodule
module m1;
logic clk;
always_ff @(posedge clk) begin end
m2 m2();
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    ModuleDefinitionVisitor defs;
    compilation.getRoot().visit(defs);

    auto const *m1 = defs.modules.at("m1");
    EXPECT_TRUE(m1);
    ProcedureBlockVisitor vis(m1, slang::ProceduralBlockKind::AlwaysFF);
    m1->visit(vis);
    EXPECT_EQ(vis.stmts.size(), 1);
    // should not visit the child
    vis = ProcedureBlockVisitor(m1, slang::ProceduralBlockKind::Final);
    m1->visit(vis);
    EXPECT_TRUE(vis.stmts.empty());

    auto const *m2 = defs.modules.at("m2");
    EXPECT_TRUE(m2);
    vis = ProcedureBlockVisitor(m2, slang::ProceduralBlockKind::Final);
    m2->visit(vis);
    EXPECT_EQ(vis.stmts.size(), 1);

    vis = ProcedureBlockVisitor(m2, slang::ProceduralBlockKind::Initial);
    m2->visit(vis);
    EXPECT_EQ(vis.stmts.size(), 1);
}

TEST(ast, timing_procedure) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic a;
always #5 a = ~a;
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    ModuleDefinitionVisitor defs;
    compilation.getRoot().visit(defs);

    auto const *m = defs.modules.at("m");
    DependencyAnalysisVisitor vis;
    m->visit(vis);
    EXPECT_EQ(vis.timed_stmts.size(), 1);
    EXPECT_TRUE(vis.graph->nodes.empty());
}