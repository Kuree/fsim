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
    error = analyze_vars();
    if (!error.empty()) return error;

    // TODO: add ports

    // compute procedure combinational blocks
    error = analyze_comb();
    if (!error.empty()) return error;

    error = analyze_init();
    if (!error.empty()) return error;

    error = analyze_final();
    if (!error.empty()) return error;

    error = analyze_ff();
    if (!error.empty()) return error;

    return error;
}

std::string Module::analyze_vars() {
    VariableDefinitionVisitor v;
    def_->visit(v);
    for (auto const *var : v.vars) {
        auto v_ = std::make_unique<Variable>();
        v_->sym = var;
        vars.emplace(var->name, std::move(v_));
    }
    return {};
}

bool is_assignment(const slang::Symbol *symbol) {
    if (symbol->kind == slang::SymbolKind::Variable) {
        return false;
    } else if (symbol->kind == slang::SymbolKind::Net) {
        auto const &net = symbol->as<slang::NetSymbol>();
        return net.getInitializer() != nullptr;
    }
    return true;
}

std::string Module::analyze_comb() {
    std::string error;
    DependencyAnalysisVisitor v(def_);
    def_->visit(v);
    auto *graph = v.graph;
    // compute a topological order
    // then merge the nodes cross procedural block boundary
    auto order = sort(graph, error);
    if (!error.empty()) return error;

    // merging nodes and create procedure blocks
    // we treat initial assignment and continuous assignments as combinational
    // block (or always_latch)
    auto process = std::make_unique<CombProcess>();
    for (auto const *n : order) {
        auto const &sym = n->symbol;
        // ignore nodes without initializer since we will create the variables/logics in a different
        // places
        if (!is_assignment(&sym)) continue;
        if (sym.kind == slang::SymbolKind::ProceduralBlock) {
            // need to flush whatever in the pipe
            if (!process->stmts.empty()) {
                comb_processes.emplace_back(std::move(process));
                process = std::make_unique<CombProcess>();
            }
            process->stmts.emplace_back(&sym);
            comb_processes.emplace_back(std::move(process));
            process = std::make_unique<CombProcess>();
        } else {
            process->stmts.emplace_back(&sym);
        }
    }
    if (!process->stmts.empty()) {
        comb_processes.emplace_back(std::move(process));
    }
    return {};
}

void extract_procedure_blocks(std::vector<std::unique_ptr<Process>> &processes,
                              const slang::InstanceSymbol *def, slang::ProceduralBlockKind kind) {
    ProcedureBlockVisitor vis(def, kind);
    def->visit(vis);
    processes.reserve(vis.stmts.size());
    for (auto const *p : vis.stmts) {
        auto &process = processes.emplace_back(
            std::move(std::make_unique<Process>(slang::ProceduralBlockKind::Initial)));
        process->stmts.emplace_back(p);
    }
}

std::string Module::analyze_init() {
    // we don't do anything to init block
    // since we just treat it as an actual fiber thread and let it do stuff
    extract_procedure_blocks(init_processes, def_, slang::ProceduralBlockKind::Initial);
    return {};
}

std::string Module::analyze_final() {
    extract_procedure_blocks(final_processes, def_, slang::ProceduralBlockKind::Final);
    return {};
}

std::string Module::analyze_ff() {
    // notice that we also use always_ff to refer to the old-fashion always block
    extract_procedure_blocks(ff_processes, def_, slang::ProceduralBlockKind::AlwaysFF);
    return {};
}

}  // namespace xsim