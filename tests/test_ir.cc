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