#include "ir.hh"

#include <stack>
#include <unordered_set>

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

    // this is a recursive call to walk through all the module definitions
    analyze_inst();

    return error;
}

std::string Module::analyze_vars() {
    VariableDefinitionVisitor v(def_);
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

// tracking changes in sensitivity list
class SensitivityListTracker {
public:
    void add_node(const DependencyAnalysisVisitor::Node *node) {
        auto const &edges_to = node->edges_to;
        auto const &edges_from = node->edges_from;

        for (auto const *n : edges_from) {
            auto const &sym = n->symbol;
            if (sym.kind == slang::SymbolKind::Variable) {
                auto const *ptr = &sym;
                if (provides.find(ptr) != provides.end()) {
                    // we're good
                    continue;
                } else {
                    nodes.emplace(ptr);
                }
            }
        }

        for (auto const *n : edges_to) {
            provides.emplace(&n->symbol);
        }
    }

    [[nodiscard]] std::vector<const slang::Symbol *> get_list() const {
        std::vector<const slang::Symbol *> result;
        result.reserve(nodes.size());
        // we don't allow self-triggering here
        //  if a variable is used for accumulation for instance
        //  a single process run is enough anyway
        for (auto const *n : nodes) {
            if (provides.find(n) == provides.end()) {
                result.emplace_back(n);
            }
        }
        std::sort(result.begin(), result.end(),
                  [](auto const *a, auto const *b) { return a->name < b->name; });
        return result;
    }

private:
    std::unordered_set<const slang::Symbol *> nodes;
    std::unordered_set<const slang::Symbol *> provides;
};

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
    SensitivityListTracker tracker;
    for (auto const *n : order) {
        auto const &sym = n->symbol;
        // ignore nodes without initializer since we will create the variables/logics in a different
        // places
        if (!is_assignment(&sym)) continue;
        if (sym.kind == slang::SymbolKind::ProceduralBlock) {
            // need to flush whatever in the pipe
            if (!process->stmts.empty()) {
                // we're creating implicit comb blocks to create
                process->kind = CombProcess::CombKind::Implicit;
                tracker.add_node(n);
                process->sensitive_list = tracker.get_list();
                comb_processes.emplace_back(std::move(process));
                process = std::make_unique<CombProcess>();
                tracker = {};
            }
            // depends on the type, we need to set the comb block carefully
            auto const &p_block = sym.as<slang::ProceduralBlockSymbol>();
            if (p_block.procedureKind == slang::ProceduralBlockKind::AlwaysLatch) {
                process->kind = CombProcess::CombKind::Latch;
            } else if (p_block.procedureKind == slang::ProceduralBlockKind::Always) {
                // need to figure out if it's implicit or explicit
                process->kind = CombProcess::CombKind::Implicit;
            }
            tracker.add_node(n);
            process->stmts.emplace_back(&sym);
            process->sensitive_list = tracker.get_list();
            comb_processes.emplace_back(std::move(process));
            process = std::make_unique<CombProcess>();
            tracker = {};
        } else {
            tracker.add_node(n);
            process->stmts.emplace_back(&sym);
        }
    }
    if (!process->stmts.empty()) {
        process->sensitive_list = tracker.get_list();
        process->kind = CombProcess::CombKind::Implicit;
        comb_processes.emplace_back(std::move(process));
    }

    // also need to optimize for general purpose block
    for (auto const *sym : v.general_always_stmts) {
        auto p = std::make_unique<CombProcess>(CombProcess::CombKind::GeneralPurpose);
        p->stmts.emplace_back(sym);
        comb_processes.emplace_back(std::move(p));
    }

    return {};
}

void extract_procedure_blocks(std::vector<std::unique_ptr<Process>> &processes,
                              const slang::InstanceSymbol *def, slang::ProceduralBlockKind kind) {
    ProcedureBlockVisitor vis(def, kind);
    def->visit(vis);
    processes.reserve(vis.stmts.size());
    for (auto const *p : vis.stmts) {
        auto &process =
            processes.emplace_back(std::make_unique<Process>(slang::ProceduralBlockKind::Initial));
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

class ModuleAnalyzeVisitor : public slang::ASTVisitor<ModuleAnalyzeVisitor, false, false> {
public:
    explicit ModuleAnalyzeVisitor(Module *target) : target_(target) {}
    [[maybe_unused]] void handle(const slang::InstanceSymbol &inst) {
        if (!error.empty()) return;
        if (target_->def() == &inst) {
            visitDefault(inst);
        } else {
            // child instance
            auto def_name = inst.getDefinition().name;
            if (module_defs_.find(def_name) == module_defs_.end()) {
                auto child = std::make_shared<Module>(&inst);
                module_defs_.emplace(def_name, child);
                // this will call the analysis function recursively
                error = child->analyze();
            }
            auto c = module_defs_.at(def_name);
            target_->child_instances.emplace(inst.name, c);
        }
    }

    std::string error;

private:
    std::unordered_map<std::string_view, std::shared_ptr<Module>> module_defs_;
    Module *target_;
};

std::string Module::analyze_inst() {
    ModuleAnalyzeVisitor vis(this);
    def_->visit(vis);
    return vis.error;
}

}  // namespace xsim