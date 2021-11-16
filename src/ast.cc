#include "ast.hh"

#include <unordered_set>

#include "fmt/format.h"

namespace xsim {

[[maybe_unused]] void ModuleDefinitionVisitor::handle(const slang::InstanceSymbol &symbol) {
    auto const &def = symbol.getDefinition();
    if (def.definitionKind == slang::DefinitionKind::Module) {
        modules.emplace(def.name, &symbol);
    }
    visitDefault(symbol);
}

class VariableExtractor : public slang::ASTVisitor<VariableExtractor, true, true> {
public:
    VariableExtractor() = default;

    [[maybe_unused]] void handle(const slang::NamedValueExpression &var) { vars.emplace(&var); }

    std::unordered_set<const slang::NamedValueExpression *> vars;
};

[[maybe_unused]] void ModuleComplexityVisitor::handle(const slang::AssignmentExpression &) {
    complexity += current_level_;
}

[[maybe_unused]] void ModuleComplexityVisitor::handle(const slang::ConditionalStatement &stmt) {
    current_level_++;
    visitDefault(stmt);
    current_level_--;
}

[[maybe_unused]] void ModuleComplexityVisitor::handle(const slang::CaseStatement &stmt) {
    current_level_++;
    visitDefault(stmt);
    current_level_--;
}

[[maybe_unused]] void ModuleComplexityVisitor::handle(const slang::ForLoopStatement &stmt) {
    current_level_++;
    visitDefault(stmt);
    current_level_--;
}

[[maybe_unused]] void VariableDefinitionVisitor::handle(const slang::VariableDeclStatement &stmt) {
    auto const &var = stmt.symbol;
    vars.emplace_back(&var);
}

DependencyAnalysisVisitor::Node *get_node_(DependencyAnalysisVisitor::Graph *graph,
                                           const slang::Symbol &sym) {
    auto n = std::string(sym.name);
    if (graph->node_mapping.find(n) == graph->node_mapping.end()) {
        auto ptr = std::make_unique<DependencyAnalysisVisitor::Node>(sym);
        auto &node = graph->nodes.emplace_back(std::move(ptr));
        graph->node_mapping.emplace(n, node.get());
    }
    return graph->node_mapping.at(n);
}

DependencyAnalysisVisitor::Node *DependencyAnalysisVisitor::Graph::get_node(
    const slang::NamedValueExpression *name) {
    auto const &sym = name->symbol;
    return get_node_(this, sym);
}

DependencyAnalysisVisitor::Node *DependencyAnalysisVisitor::Graph::get_node(
    const std::string &name) const {
    if (node_mapping.find(name) != node_mapping.end()) {
        return node_mapping.at(name);
    } else {
        return nullptr;
    }
}

DependencyAnalysisVisitor::Node *DependencyAnalysisVisitor::Graph::get_node(
    const slang::Symbol &symbol) {
    switch (symbol.kind) {
        case slang::SymbolKind::ProceduralBlock: {
            // procedural block doesn't have a name
            // we will only call it once since
            if (new_names_.find(&symbol) == new_names_.end()) {
                std::string name = fmt::format(".blk{0}", procedural_blk_count_++);
                new_names_.emplace(&symbol, name);
                auto &node = nodes.emplace_back(std::move(std::make_unique<Node>(symbol)));
                node_mapping.emplace(name, node.get());
            }
            return node_mapping.at(new_names_.at(&symbol));
        }
        case slang::SymbolKind::Variable:
        case slang::SymbolKind::Net: {
            return get_node_(this, symbol);
        }
        default: {
            throw std::runtime_error(
                fmt::format("Unsupported node {0}", slang::toString(symbol.kind)));
        }
    }
}

DependencyAnalysisVisitor::DependencyAnalysisVisitor() {
    graph_ = std::make_unique<Graph>();
    graph = graph_.get();
}

void process_assignment(const slang::AssignmentExpression &assign,
                        DependencyAnalysisVisitor::Graph *graph, std::string &error) {
    // left depends on the right-hand side
    VariableExtractor v;
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
                std::string buf;
                r->symbol.getHierarchicalPath(buf);
                error = fmt::format("Combination loop detected for {0}", buf);
            } else {
                auto *r_node = graph->get_node(r);
                auto *l_node = graph->get_node(l);
                r_node->edges_to.emplace(l_node);
                l_node->edges_from.emplace(r_node);
            }
        }
    }
}

[[maybe_unused]] void DependencyAnalysisVisitor::handle(const slang::ContinuousAssignSymbol &stmt) {
    auto const &assign = stmt.getAssignment().as<slang::AssignmentExpression>();
    process_assignment(assign, graph, error);
}

class SensitivityListExtraction : public slang::ASTVisitor<SensitivityListExtraction, true, true> {
public:
    [[maybe_unused]] void handle(const slang::ExpressionStatement &s) {
        auto const &expr = s.expr;
        if (expr.kind == slang::ExpressionKind::Assignment) {
            auto const &assign = expr.as<slang::AssignmentExpression>();
            VariableExtractor v;
            auto const &left_expr = assign.left();
            auto const &right_expr = assign.right();
            left_expr.visit(v);
            for (auto *var : v.vars) left.emplace(var);
            v = {};
            right_expr.visit(v);
            for (auto *var : v.vars) right.emplace(var);
        }

        visitDefault(s);
    }

    [[maybe_unused]] void handle(const slang::ConditionalStatement &if_) {
        auto const &expr = if_.cond;
        VariableExtractor v;
        expr.visit(v);
        for (auto *var : v.vars) right.emplace(var);
        visitDefault(if_);
    }

    [[maybe_unused]] void handle(const slang::CaseStatement &case_) {
        auto const &expr = case_.expr;
        VariableExtractor v;
        expr.visit(v);
        for (auto *var : v.vars) right.emplace(var);
        visitDefault(case_);
    }

    std::unordered_set<const slang::NamedValueExpression *> left;
    std::unordered_set<const slang::NamedValueExpression *> right;
};

[[maybe_unused]] void DependencyAnalysisVisitor::handle(const slang::ProceduralBlockSymbol &stmt) {
    if (stmt.procedureKind == slang::ProceduralBlockKind::AlwaysComb ||
        stmt.procedureKind == slang::ProceduralBlockKind::Always ||
        stmt.procedureKind == slang::ProceduralBlockKind::AlwaysLatch) {
        // we need to compute analysis on the right-hand side, as well as if and switch
        // conditions.
        // then we create a node in the graph to represent the node
        auto *node = graph->get_node(stmt);
        SensitivityListExtraction s;
        stmt.visit(s);
        for (auto *left : s.left) {
            auto n = graph->get_node(left);
            node->edges_to.emplace(n);
            n->edges_from.emplace(node);
        }
        for (auto *right : s.right) {
            auto n = graph->get_node(right);
            node->edges_from.emplace(n);
            n->edges_to.emplace(node);
        }
    }
    visitDefault(stmt);
}

void add_init_node(const slang::Expression *expr, const slang::Symbol &var,
                   DependencyAnalysisVisitor::Graph *graph) {
    VariableExtractor v;
    expr->visit(v);
    auto *left = graph->get_node(var);
    auto const &right = v.vars;
    for (auto const *node : right) {
        auto *n = graph->get_node(node);
        left->edges_from.emplace(n);
        n->edges_to.emplace(left);
    }
}

// NOLINTNEXTLINE
[[maybe_unused]] void DependencyAnalysisVisitor::handle(const slang::VariableSymbol &var) {
    // notice that we're only interested in the module level variables
    if (!var.getParentScope()->isProceduralContext() && var.getInitializer()) {
        // it also has to have an initializer
        add_init_node(var.getInitializer(), var, graph);
    }
}

// NOLINTNEXTLINE
[[maybe_unused]] void DependencyAnalysisVisitor::handle(const slang::NetSymbol &sym) {
    // notice that we're only interested in the module level variables
    if (!sym.getParentScope()->isProceduralContext() && sym.getInitializer()) {
        // it also has to have an initializer
        add_init_node(sym.getInitializer(), sym, graph);
    }
}

}  // namespace xsim