#include "ast.hh"

#include <unordered_set>

#include "except.hh"
#include "fmt/format.h"

namespace fsim {

[[maybe_unused]] void ModuleDefinitionVisitor::handle(const slang::InstanceSymbol &symbol) {
    auto const &def = symbol.getDefinition();
    if (def.definitionKind == slang::DefinitionKind::Module) {
        modules.emplace(def.name, &symbol);
    }
    visitDefault(symbol);
}

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
        case slang::SymbolKind::ProceduralBlock:
        case slang::SymbolKind::ContinuousAssign: {
            // procedural block doesn't have a name
            // we will only call it once since
            if (new_names_.find(&symbol) == new_names_.end()) {
                std::string name = fmt::format(".blk{0}", procedural_blk_count_++);
                new_names_.emplace(&symbol, name);
                auto &node = nodes.emplace_back(std::make_unique<Node>(symbol));
                node_mapping.emplace(name, node.get());
            }
            return node_mapping.at(new_names_.at(&symbol));
        }
        case slang::SymbolKind::Variable:
        case slang::SymbolKind::Net: {
            return get_node_(this, symbol);
        }
        default: {
            throw NotSupportedException(
                fmt::format("Unsupported node {0}", slang::toString(symbol.kind)), symbol.location);
        }
    }
}

DependencyAnalysisVisitor::DependencyAnalysisVisitor(const slang::Symbol *target)
    : target_(target) {
    graph_ = std::make_unique<Graph>();
    graph = graph_.get();
}

// NOLINTNEXTLINE
[[maybe_unused]] void DependencyAnalysisVisitor::handle(const slang::ContinuousAssignSymbol &stmt) {
    auto const &assign = stmt.getAssignment().as<slang::AssignmentExpression>();

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
    // we treat this as a new node
    // since it is a continuous assignment, which needs to be put into
    // always block
    auto *new_node = graph->get_node(stmt);
    for (auto const *r : right_vars) {
        // we don't care about variable declared in procedural
        if (r->symbol.getParentScope()->isProceduralContext()) continue;
        for (auto const *l : left_vars) {
            if (l->symbol.getParentScope()->isProceduralContext()) continue;
            if (r == l) {
                continue;
            } else {
                auto *r_node = graph->get_node(r);
                auto *l_node = graph->get_node(l);

                r_node->edges_to.emplace(new_node);
                new_node->edges_to.emplace(l_node);

                l_node->edges_from.emplace(new_node);
                new_node->edges_from.emplace(r_node);
            }
        }
    }
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

// See LRM 9.2.2.2.1
// TODO: implement longest prefix (11.5.3)
std::pair<bool, std::unordered_set<const slang::NamedValueExpression *>>
extract_combinational_sensitivity(const slang::ProceduralBlockSymbol *stmt) {
    // old-fashioned way of extracting out always block's sensitivity list
    auto const &body = stmt->getBody();
    if (body.kind != slang::StatementKind::Timed) {
        return {false, {}};
    }
    auto const &timed = body.as<slang::TimedStatement>();
    auto const &timing_control = timed.timing;
    std::unordered_set<const slang::NamedValueExpression *> result;
    if (timing_control.kind == slang::TimingControlKind::SignalEvent) {
        // maybe?
        auto const &signal_event = timing_control.as<slang::SignalEventControl>();
        if (signal_event.edge == slang::EdgeKind::None) {
            auto const &var = signal_event.expr;
            VariableExtractor vis;
            var.visit(vis);
            for (auto const *v : vis.vars) {
                result.emplace(v);
            }
            return {true, result};
        }
        return {false, {}};
    } else if (timing_control.kind == slang::TimingControlKind::ImplicitEvent) {
        // need to use the old way to extract out sensitivity
        return {true, {}};
    } else if (timing_control.kind == slang::TimingControlKind::EventList) {
        auto const &event_list = timing_control.as<slang::EventListControl>();
        for (auto const *event : event_list.events) {
            if (event->kind == slang::TimingControlKind::SignalEvent) {
                auto const &sig = event->as<slang::SignalEventControl>();
                if (sig.edge == slang::EdgeKind::None) {
                    VariableExtractor vis;
                    sig.expr.visit(vis);
                    for (auto const *v : vis.vars) {
                        result.emplace(v);
                    }
                }
            }
        }
        if (result.empty()) {
            return {false, {}};
        } else {
            return {true, result};
        }
    } else {
        return {false, {}};
    }
}

class TimingControlVisitor : public slang::ASTVisitor<TimingControlVisitor, true, true> {
public:
    bool has_timing_control = false;
    [[maybe_unused]] void handle(const slang::AssignmentExpression &assignment) {
        if (assignment.timingControl) has_timing_control = true;
    }
    [[maybe_unused]] void handle(const slang::TimingControl &) { has_timing_control = true; }
};

// always blocks with timing control
// used for infinite loop
// notice that always_comb doesn't allow delay, see LRM 9.2.2.2.2
bool has_timing_control(const slang::ProceduralBlockSymbol &stmt) {
    TimingControlVisitor vis;
    stmt.visit(vis);
    return vis.has_timing_control;
}

// NOLINTNEXTLINE
[[maybe_unused]] void DependencyAnalysisVisitor::handle(const slang::ProceduralBlockSymbol &stmt) {
    if (stmt.procedureKind == slang::ProceduralBlockKind::AlwaysComb ||
        stmt.procedureKind == slang::ProceduralBlockKind::Always ||
        stmt.procedureKind == slang::ProceduralBlockKind::AlwaysLatch) {
        // legacy verilog is pain
        std::unordered_set<const slang::NamedValueExpression *> right_list;
        if (stmt.procedureKind == slang::ProceduralBlockKind::Always) {
            auto [comb, lst] = extract_combinational_sensitivity(&stmt);
            if (comb) {
                if (!lst.empty()) {
                    // use that instead
                    right_list = lst;
                }
            } else {
                // TODO:
                //  report as an error if no timing control found
                if (has_timing_control(stmt)) {
                    general_always_stmts.emplace_back(&stmt);
                }
                return;
            }
        }
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
        if (right_list.empty()) right_list = s.right;
        for (auto *right : right_list) {
            auto n = graph->get_node(right);
            node->edges_from.emplace(n);
            n->edges_to.emplace(node);
        }
    }
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
[[maybe_unused]] void DependencyAnalysisVisitor::handle(const slang::NetSymbol &sym) {
    // notice that we're only interested in the module level variables
    if (!sym.getParentScope()->isProceduralContext() && sym.getInitializer()) {
        // it also has to have an initializer
        add_init_node(sym.getInitializer(), sym, graph);
    }
}

[[maybe_unused]] void DependencyAnalysisVisitor::handle(const slang::InstanceSymbol &sym) {
    // notice that we don't do anything here to prevent the visitor visit deeper into
    // the hierarchy since the child module definition will be handled by other module
    // instances
    if (target_ && &sym != target_) {
        return;
    }
    visitDefault(sym);
}

[[maybe_unused]] void ProcedureBlockVisitor::handle(const slang::InstanceSymbol &symbol) {
    if (target_ == &symbol) {
        visitDefault(symbol);
    }
}

[[maybe_unused]] void ProcedureBlockVisitor::handle(const slang::ProceduralBlockSymbol &stmt) {
    if (stmt.procedureKind == kind_) {
        stmts.emplace_back(&stmt);
    }
    // no need to visit inside
}

void FunctionCallVisitor::handle(const slang::CallExpression &call) {
    auto const &func = call.subroutine;
    if (func.index() == 0) {
        auto *ptr = std::get<0>(func);
        functions.emplace(ptr);
    }
    visitDefault(call);
}

[[maybe_unused]] void FunctionCallVisitor::handle(const slang::InstanceSymbol &symbol) {
    if (target_ == &symbol) {
        visitDefault(symbol);
    }
}

}  // namespace fsim