#include "ast.hh"

#include <unordered_set>

namespace xsim {

[[maybe_unused]] void ModuleDefinitionVisitor::handle(const slang::InstanceSymbol &symbol) {
    auto const &def = symbol.getDefinition();
    if (def.definitionKind == slang::DefinitionKind::Module) {
        modules.emplace(def.name, &def);
        visitDefault(symbol);
    }
}

class VariableExtractor : public slang::ASTVisitor<VariableExtractor, true, true> {
public:
    VariableExtractor() = default;

    [[maybe_unused]] void handle(const slang::NamedValueExpression &var) { vars.emplace(&var); }

    std::unordered_set<const slang::NamedValueExpression *> vars;
};

DependencyAnalysisVisitor::Node *DependencyAnalysisVisitor::Graph::get_node(
    const slang::NamedValueExpression *name) {
    auto const &sym = name->symbol;
    if (node_mapping.find(sym.name) == node_mapping.end()) {
        auto &node = nodes.emplace_back(std::make_unique<Node>());
        node->name = sym.name;
        node_mapping.emplace(sym.name, node.get());
    }
    return node_mapping.at(sym.name);
}

DependencyAnalysisVisitor::DependencyAnalysisVisitor() {
    graph_ = std::make_unique<Graph>();
    graph = graph_.get();
}

[[maybe_unused]] void DependencyAnalysisVisitor::handle(const slang::ContinuousAssignSymbol &stmt) {
    // left depends on the right-hand side
    VariableExtractor v;
    auto const &assign = stmt.getAssignment().as<slang::AssignmentExpression>();
    auto const &left_expr = assign.left();
    auto const &right_expr = assign.right();
    left_expr.visit(v);
    auto left_vars = v.vars;
    v = {};
    right_expr.visit(v);
    auto right_vars = v.vars;

    // compute the dependency graph
    for (auto const *r : right_vars) {
        for (auto const *l : left_vars) {
            if (r == l) {
                std::string syntax;
                if (stmt.getSyntax()) {
                    syntax = stmt.getSyntax()->sourceRange().start().bufferName;
                }
                error = "Combination loop detected";
                if (!syntax.empty()) {
                    error.append(" ").append(syntax);
                }
            } else {
                auto *r_node = graph->get_node(r);
                auto *l_node = graph->get_node(l);
                r_node->edges_to.emplace_back(l_node);
                l_node->edges_from.emplace_back(r_node);
            }
        }
    }
}

void DependencyAnalysisVisitor::handle(const slang::ProceduralBlockSymbol &stmt) {
    if (stmt.procedureKind == slang::ProceduralBlockKind::AlwaysComb ||
        stmt.procedureKind == slang::ProceduralBlockKind::Always ||
        stmt.procedureKind == slang::ProceduralBlockKind::AlwaysLatch) {
        // we need to compute analysis on the right-hand side, as well as if and switch
        // conditions
    }
}

}  // namespace xsim