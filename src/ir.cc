#include "ir.hh"

#include <stack>

#include "ast.hh"
#include "fmt/format.h"

namespace xsim {

using DGraph = DependencyAnalysisVisitor::Graph;
using DNode = DependencyAnalysisVisitor::Node;

void sort_(const DNode *node, std::unordered_set<const DNode *> &visited,
           std::stack<const DNode *> &stack) {
    visited.emplace(node);
    for (auto const *next : node->edges_to) {
        if (visited.find(next) == visited.end()) {
            sort_(next, visited, stack);
        }
    }
    stack.emplace(node);
}

std::vector<const DependencyAnalysisVisitor::Node *> sort(const DGraph *graph, std::string &error) {
    std::stack<const DNode *> stack;
    std::unordered_set<const DNode *> visited;
    for (auto const &n : graph->nodes) {
        if (visited.find(n.get()) == visited.end()) {
            sort_(n.get(), visited, stack);
            if (!error.empty()) {
                return {};
            }
        }
    }

    // popping the stack
    std::vector<const DependencyAnalysisVisitor::Node *> result;
    result.reserve(graph->nodes.size());
    while (!stack.empty()) {
        result.emplace_back(stack.top());
        stack.pop();
    }
    // check loop ordering
    std::unordered_map<const DNode *, uint64_t> ordering;
    uint64_t i = 0;
    for (auto const *n : result) {
        ordering.emplace(n, i++);
    }
    for (auto const *n : result) {
        auto current = ordering.at(n);
        for (auto const *next : n->edges_to) {
            if (current > ordering.at(next)) {
                // cycle detected
                std::string buf;
                n->symbol.getHierarchicalPath(buf);
                error = fmt::format("Combinational loop detected at {0}", buf);
                return {};
            }
        }
    }
    return result;
}

std::string Module::analyze() {
    std::string error;
    // extract out variable definitions and procedures
    {
        VariableDefinitionVisitor v;
        def_->visit(v);
        for (auto const *var : v.vars) {
            auto v_ = std::make_unique<Variable>();
            v_->sym = var;
            vars.emplace(var->name, std::move(v_));
        }
    }

    // TODO: add ports

    // compute procedure combinational blocks
    {
        DependencyAnalysisVisitor v;
        def_->visit(v);
        auto *graph = v.graph;
        // compute a topological order
        // then merge the nodes cross procedural block boundary
        auto order = sort(graph, error);
        if (!error.empty()) return error;

        // merging nodes and create procedure blocks
        auto process = std::make_unique<Process>();
        for (auto const *n : order) {
            auto const &sym = n->symbol;
            if (sym.kind == slang::SymbolKind::ProceduralBlock) {
                // need to flush whatever in the pipe
                if (!process->stmts.empty()) {
                    processes.emplace_back(std::move(process));
                    process = std::make_unique<Process>();
                }
            }
            process->stmts.emplace_back(&sym);
        }
        if (!process->stmts.empty()) {
            processes.emplace_back(std::move(process));
        }
    }
    return error;
}

}  // namespace xsim