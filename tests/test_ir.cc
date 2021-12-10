#include "../src/ast.hh"
#include "../src/ir.hh"
#include "gtest/gtest.h"
#include "slang/syntax/SyntaxTree.h"

using namespace xsim;
using namespace slang;

TEST(ir, module_process) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic a, b, c;
wire d = c;
assign c = b;
always_comb
    b = a;
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    ModuleDefinitionVisitor vis;
    compilation.getRoot().visit(vis);
    auto *def = vis.modules.at("m");
    Module m(def);
    auto error = m.analyze();
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(m.comb_processes.size(), 2);
    EXPECT_EQ(m.comb_processes[1]->stmts.size(), 2);

    EXPECT_EQ(m.comb_processes[0]->stmts[0]->kind, SymbolKind::ProceduralBlock);
    EXPECT_EQ(m.comb_processes[1]->stmts[0]->kind, SymbolKind::ContinuousAssign);
    EXPECT_EQ(m.comb_processes[1]->stmts[1]->kind, SymbolKind::Net);

    EXPECT_EQ(m.comb_processes[0]->kind, CombProcess::CombKind::AlwaysComb);
    EXPECT_EQ(m.comb_processes[1]->kind, CombProcess::CombKind::Implicit);

    // the first process (always_comb) should only depend on a
    // and the second process (merged block) should only depend on b
    EXPECT_EQ(m.comb_processes[0]->sensitive_list.size(), 1);
    EXPECT_EQ(m.comb_processes[0]->sensitive_list[0]->name, "a");
    EXPECT_EQ(m.comb_processes[1]->sensitive_list.size(), 1);
    EXPECT_EQ(m.comb_processes[1]->sensitive_list[0]->name, "b");
}

TEST(ir, module_always_ff) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module m;
logic a, b, c;
always_ff @(posedge c) begin
end
always @(posedge a, negedge b) begin
end
always @(posedge a, negedge a) begin
end
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    ModuleDefinitionVisitor vis;
    compilation.getRoot().visit(vis);
    auto *def = vis.modules.at("m");
    Module m(def);
    auto error = m.analyze();
    EXPECT_TRUE(error.empty());

    EXPECT_EQ(m.ff_processes.size(), 3);
    EXPECT_EQ(m.ff_processes[0]->edges.size(), 1);
    EXPECT_EQ(m.ff_processes[0]->edges[0].first, slang::EdgeKind::PosEdge);
    EXPECT_EQ(m.ff_processes[1]->edges.size(), 2);
    EXPECT_EQ(m.ff_processes[1]->edges[0].second->name, "a");
    EXPECT_EQ(m.ff_processes[2]->edges.size(), 2);
    EXPECT_EQ(m.ff_processes[2]->edges[1].first, slang::EdgeKind::NegEdge);
    EXPECT_EQ(m.ff_processes[2]->edges[0].second->name, "a");
}

TEST(ir, child_inst) {  // NOLINT
    auto tree = SyntaxTree::fromText(R"(
module child (input logic a, b, output logic c);
assign c = a & b;
endmodule
module m;
logic a, b, c;
child inst (.*);
endmodule
)");
    Compilation compilation;
    compilation.addSyntaxTree(tree);
    ModuleDefinitionVisitor vis;
    compilation.getRoot().visit(vis);
    auto *def = vis.modules.at("m");
    Module m(def);
    auto error = m.analyze();
    EXPECT_TRUE(error.empty());

    auto const &child = m.child_instances.at("inst");
    EXPECT_EQ(child->inputs.size(), 2);
    EXPECT_EQ(child->outputs.size(), 1);
}