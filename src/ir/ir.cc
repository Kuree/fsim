#include "ir.hh"

#include <set>
#include <stack>
#include <unordered_set>

#include "ast.hh"
#include "fmt/format.h"

namespace xsim {

using DGraph = DependencyAnalysisVisitor::Graph;
using DNode = DependencyAnalysisVisitor::Node;

// NOLINTNEXTLINE
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
    error = analyze_connections();
    if (!error.empty()) return error;

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

class PortVariableSymbolCollector
    : public slang::ASTVisitor<PortVariableSymbolCollector, false, false> {
public:
    PortVariableSymbolCollector(const std::vector<Module::PortDef> &in,
                                const std::vector<Module::PortDef> &out) {
        for (auto const &[p, _] : in) {
            names_.emplace(p->name);
        }
        for (auto const &[p, _] : out) {
            names_.emplace(p->name);
        }
    }

    [[maybe_unused]] void handle(const slang::VariableSymbol &sym) {
        if (names_.find(sym.name) != names_.end()) {
            port_vars.emplace(sym.name, &sym);
        }
    }

    [[maybe_unused]] void handle(const slang::InstanceBodySymbol &sym) {
        if (!names_.empty() && !instance_) {
            instance_ = &sym;
            visitDefault(sym);
        }
    }

    std::unordered_map<std::string_view, const slang::VariableSymbol *> port_vars;

private:
    std::unordered_set<std::string_view> names_;
    const slang::InstanceBodySymbol *instance_ = nullptr;
};

std::string Module::analyze_connections() {
    // we only care about ports for now
    auto const &port_list = def_->body.getPortList();
    for (auto const *sym : port_list) {
        if (slang::PortSymbol::isKind(sym->kind)) {
            auto const &port = sym->as<slang::PortSymbol>();
            auto connection = def_->getPortConnection(port);
            auto const *expr = connection->getExpression();
            switch (port.direction) {
                case slang::ArgumentDirection::In: {
                    inputs.emplace_back(std::make_pair(&port, expr));
                    break;
                }
                case slang::ArgumentDirection::Out: {
                    outputs.emplace_back(std::make_pair(&port, expr));
                    break;
                }
                default:
                    return fmt::format("Unsupported port direction {0}",
                                       slang::toString(port.direction));
            }
        }
    }

    // need to collect port variable symbols. slang treat port and variable symbols differently
    PortVariableSymbolCollector visitor(inputs, outputs);
    def_->body.visit(visitor);
    port_vars = std::move(visitor.port_vars);
    return {};
}

class EdgeEventControlVisitor : public slang::ASTVisitor<EdgeEventControlVisitor, true, true> {
public:
    [[maybe_unused]] void handle(const slang::TimedStatement &stmt) {
        auto const &timing = stmt.timing;
        if (timing.kind == slang::TimingControlKind::SignalEvent) {
            auto const &event = timing.as<slang::SignalEventControl>();
            auto const &expr = event.expr;
            if (expr.kind != slang::ExpressionKind::NamedValue) {
                throw std::runtime_error("Only single named value event supported");
            }
            auto const &named_value = expr.as<slang::NamedValueExpression>();
            auto const &var = named_value.symbol;
            vars.emplace(std::make_pair(&var, event.edge));
        }
    }

    std::set<std::pair<const slang::ValueSymbol *, const slang::EdgeKind>> vars;
};

void analyze_edge_event_control(Process *process) {
    EdgeEventControlVisitor visitor;
    for (auto const *stmt : process->stmts) {
        stmt->visit(visitor);
    }

    process->edge_event_controls =
        std::vector<std::pair<const slang::ValueSymbol *, slang::EdgeKind>>(visitor.vars.begin(),
                                                                            visitor.vars.end());
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

    for (auto const &p : comb_processes) {
        analyze_edge_event_control(p.get());
    }

    return {};
}

void extract_procedure_blocks(std::vector<std::unique_ptr<Process>> &processes,
                              const slang::InstanceSymbol *def, slang::ProceduralBlockKind kind) {
    ProcedureBlockVisitor vis(def, kind);
    def->visit(vis);
    processes.reserve(vis.stmts.size());
    for (auto const *p : vis.stmts) {
        auto &process = processes.emplace_back(std::make_unique<Process>(kind));
        process->stmts.emplace_back(p);
    }
}

std::string Module::analyze_init() {
    // we don't do anything to init block
    // since we just treat it as an actual fiber thread and let it do stuff
    extract_procedure_blocks(init_processes, def_, slang::ProceduralBlockKind::Initial);
    for (auto const &p : init_processes) {
        analyze_edge_event_control(p.get());
    }
    return {};
}

std::string Module::analyze_final() {
    extract_procedure_blocks(final_processes, def_, slang::ProceduralBlockKind::Final);
    return {};
}

std::string Module::analyze_ff() {
    // notice that we also use always_ff to refer to the old-fashion always block
    std::vector<const slang::ProceduralBlockSymbol *> stmts;
    {
        ProcedureBlockVisitor vis(def_, slang::ProceduralBlockKind::AlwaysFF);
        def_->visit(vis);
        stmts.reserve(stmts.size() + vis.stmts.size());
        for (auto const *p : vis.stmts) {
            stmts.emplace_back(p);
        }
    }

    {
        // old-fashioned always
        ProcedureBlockVisitor vis(def_, slang::ProceduralBlockKind::Always);
        def_->visit(vis);
        stmts.reserve(stmts.size() + vis.stmts.size());
        for (auto const *p : vis.stmts) {
            stmts.emplace_back(p);
        }
    }

    // look into each block to see if there is any match
    for (auto const *stmt : stmts) {
        auto const &body = stmt->getBody();
        if (body.kind != slang::StatementKind::Timed) {
            continue;
        }

        auto const &timed_body = body.as<slang::TimedStatement>();
        auto const &timing_control = timed_body.timing;

        std::vector<std::pair<slang::EdgeKind, const slang::ValueSymbol *>> edges;

        if (timing_control.kind == slang::TimingControlKind::SignalEvent) {
            auto const &single_event = timing_control.as<slang::SignalEventControl>();
            if (single_event.edge != slang::EdgeKind::None) {
                // we only deal with named expression
                if (single_event.expr.kind != slang::ExpressionKind::NamedValue) {
                    return "Only named value supported in the always_ff block";
                }
                auto const &named = single_event.expr.as<slang::NamedValueExpression>();
                edges.emplace_back(std::make_pair(single_event.edge, &named.symbol));
            } else if (single_event.edge == slang::EdgeKind::BothEdges) {
                return "Both edges not supported";
            }
        } else {
            auto const &event_list = timing_control.as<slang::EventListControl>();
            for (auto const &event : event_list.events) {
                if (event->kind == slang::TimingControlKind::SignalEvent) {
                    auto const &single_event = event->as<slang::SignalEventControl>();
                    if (single_event.edge != slang::EdgeKind::None) {
                        // we only deal with named expression
                        if (single_event.expr.kind != slang::ExpressionKind::NamedValue) {
                            return "Only named value supported in the always_ff block";
                        }
                        auto const &named = single_event.expr.as<slang::NamedValueExpression>();
                        edges.emplace_back(std::make_pair(single_event.edge, &named.symbol));
                    }
                }
            }
        }

        if (!edges.empty()) {
            auto &process = ff_processes.emplace_back(std::make_unique<FFProcess>());
            process->edges = edges;
            process->stmts.emplace_back(stmt);
        }
    }

    for (auto const &p : ff_processes) {
        analyze_edge_event_control(p.get());
    }

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
            auto const &def = inst.getDefinition();
            if (def.definitionKind == slang::DefinitionKind::Module) {
                // child instance
                auto def_name = inst.getDefinition().name;
                // TODO. deal with parametrization
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

// NOLINTNEXTLINE
void get_defs(const Module *module, std::unordered_set<const Module *> &result) {
    result.emplace(module);
    for (auto const &[_, inst] : module->child_instances) {
        get_defs(inst.get(), result);
    }
}

std::unordered_set<const Module *> Module::get_defs() const {
    std::unordered_set<const Module *> result;
    ::xsim::get_defs(this, result);
    return result;
}

void add_edge_control_tracked_var(const Process *process,
                                  std::unordered_set<std::string_view> &result) {
    for (auto const &[v, _] : process->edge_event_controls) {
        result.emplace(v->name);
    }
}

std::unordered_set<std::string_view> Module::get_tracked_vars() const {
    std::unordered_set<std::string_view> result;
    for (auto const &comb : comb_processes) {
        // label sensitivities variables
        for (auto const *sym : comb->sensitive_list) {
            result.emplace(sym->name);
        }
        add_edge_control_tracked_var(comb.get(), result);
    }

    for (auto const &ff : ff_processes) {
        for (auto const &[_, v] : ff->edges) {
            result.emplace(v->name);
        }
        add_edge_control_tracked_var(ff.get(), result);
    }

    for (auto const &init : init_processes) {
        add_edge_control_tracked_var(init.get(), result);
    }

    // any outputs need to be tracked
    for (auto const &iter : outputs) {
        result.emplace(iter.first->name);
    }

    // any expr connected to child instance's inputs needs to be tracked
    for (auto const &iter : child_instances) {
        auto const &is = iter.second->inputs;
        for (auto const &[_, expr] : is) {
            // need to figure out any named expressions
            VariableExtractor e;
            expr->visit(e);
            for (auto const &n : e.vars) {
                result.emplace(n->symbol.name);
            }
        }
    }

    return result;
}

const slang::Compilation *Module::get_compilation() const {
    if (!def_) return nullptr;
    return &def_->getParentScope()->getCompilation();
}

}  // namespace xsim