#include "cxx.hh"

#include <filesystem>
#include <set>

#include "../ir/ast.hh"
#include "dpi.hh"
#include "slang/symbols/VariableSymbols.h"
#include "stmt.hh"
#include "util.hh"

namespace fsim {

// need to generate header information about module declaration
// this includes variable, port, and parameter definition

auto constexpr raw_header_include = R"(#include "logic/array.hh"
#include "logic/logic.hh"
#include "logic/struct.hh"
#include "logic/union.hh"
#include "runtime/module.hh"
#include "runtime/system_task.hh"
#include "runtime/variable.hh"

// forward declaration
namespace fsim::runtime {
class Scheduler;
}

// for logic we use using literal namespace to make codegen simpler
using namespace logic::literals;

)";

template <typename T>
void codegen_sym(std::ostream &s, const T *sym, const CXXCodeGenOptions &options,
                 CodeGenModuleInformation &info) {
    StmtCodeGenVisitor v(s, options, info);
    sym->visit(v);
}

void codegen_edge_control(std::ostream &s, const Process *process, CodeGenModuleInformation &info) {
    ExprCodeGenVisitor v(s, info);
    if (!process->edge_event_controls.empty()) {
        slang::SourceRange sr;
        for (auto const &[var, _] : process->edge_event_controls) {
            auto name = slang::NamedValueExpression(*var, sr);
            name.visit(v);
            s << ".track_edge = true;" << std::endl;

            s << info.scheduler_name() << "->add_tracked_var(&";
            name.visit(v);
            s << ");" << std::endl;
        }
        s << fmt::format("scheduler->add_process_edge_control({0});", info.current_process_name())
          << std::endl;
    }
}

void codegen_init(std::ostream &s, const Process *process, const CXXCodeGenOptions &options,
                  CodeGenModuleInformation &info) {
    s << "{" << std::endl;

    auto const &ptr_name = info.enter_process();
    s << fmt::format("auto {0} = {1}->create_init_process();", ptr_name, info.scheduler_name())
      << std::endl
      << fmt::format("{0}->func = [this, {0}, {1}]() {{", ptr_name, info.scheduler_name())
      << std::endl;

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, stmt, options, info);
    }

    s << FSIM_END_PROCESS << "(" << ptr_name << ");" << std::endl;
    s << "};" << std::endl;
    s << fmt::format("fsim::runtime::Scheduler::schedule_init({0});", ptr_name) << std::endl;
    s << fmt::format("init_processes_.emplace_back({0});", ptr_name) << std::endl;

    codegen_edge_control(s, process, info);

    info.exit_process();
    s << "}" << std::endl;
}

void codegen_final(std::ostream &s, const Process *process, const CXXCodeGenOptions &options,
                   CodeGenModuleInformation &info) {
    s << "{" << std::endl;
    auto const &ptr_name = info.enter_process();

    s << fmt::format("auto {0} = {1}->create_final_process();", ptr_name, info.scheduler_name())
      << std::endl
      << fmt::format("{0}->func = [this, {0}, {1}]() {{", ptr_name, info.scheduler_name())
      << std::endl;

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, stmt, options, info);
    }

    s << "};" << std::endl
      << fmt::format("fsim::runtime::Scheduler::schedule_final({0});", ptr_name) << std::endl;

    info.exit_process();
    s << "}" << std::endl;
}

void codegen_always(std::ostream &s, const CombProcess *process, const CXXCodeGenOptions &options,
                    CodeGenModuleInformation &info) {
    s << "{" << std::endl;
    auto const &ptr_name = info.enter_process();

    // depends on the comb process type, we may generate different style
    bool infinite_loop = process->kind == CombProcess::CombKind::GeneralPurpose;
    // declare the always block

    s << fmt::format("auto {0} = {1}->create_comb_process();", ptr_name, info.scheduler_name())
      << std::endl
      << fmt::format("{0}->func = [this, {0}, {1}]() {{", ptr_name, info.scheduler_name())
      << std::endl;

    if (infinite_loop) {
        s << "while (true) {" << std::endl;
    }

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, stmt, options, info);
    }

    // general purpose always doesn't have end process since it never ends
    if (!infinite_loop) s << FSIM_END_PROCESS << "(" << ptr_name << ");" << std::endl;

    if (infinite_loop) {
        s << "}" << std::endl;
    }

    s << "};" << std::endl;

    // set input changed
    for (auto *var : process->sensitive_list) {
        ExprCodeGenVisitor v(s, info);
        var->visit(v);
        s << ".comb_processes.emplace_back(" << ptr_name << ");" << std::endl;
    }

    s << fmt::format("comb_processes_.emplace_back({0});", ptr_name) << std::endl;

    codegen_edge_control(s, process, info);

    info.exit_process();
    s << "}" << std::endl;
}

void codegen_ff(std::ostream &s, const FFProcess *process, const CXXCodeGenOptions &options,
                CodeGenModuleInformation &info) {
    s << "{" << std::endl;
    auto const &ptr_name = info.enter_process();

    s << fmt::format("auto {0} = {1}->create_ff_process();", ptr_name, info.scheduler_name())
      << std::endl
      << fmt::format("{0}->func = [this, {0}, {1}]() {{", ptr_name, info.scheduler_name())
      << std::endl;

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, stmt, options, info);
    }

    // output end process
    s << FSIM_END_PROCESS << "(" << ptr_name << ");" << std::endl;

    s << "};" << std::endl;

    s << fmt::format("ff_process_.emplace_back({0});", ptr_name) << std::endl;

    // generate edge trigger functions
    for (auto const &[edge, v] : process->edges) {
        if (edge == slang::EdgeKind::PosEdge || edge == slang::EdgeKind::BothEdges) {
            s << info.get_identifier_name(v->name) << ".ff_posedge_processes.emplace_back("
              << ptr_name << ");" << std::endl;
        }
        if (edge == slang::EdgeKind::NegEdge || edge == slang::EdgeKind::BothEdges) {
            s << info.get_identifier_name(v->name) << ".ff_negedge_processes.emplace_back("
              << ptr_name << ");" << std::endl;
        }
    }

    // set edge tracking as well
    std::set<std::string_view> vars;
    for (auto const &iter : process->edges) {
        vars.emplace(info.get_identifier_name(iter.second->name));
    }
    for (auto const &name : vars) {
        s << name << ".track_edge = true;" << std::endl;
    }

    codegen_edge_control(s, process, info);

    info.exit_process();
    s << "}" << std::endl;
}

void codegen_port_connections(std::ostream &s, const Module *module,
                              const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    // we generate it as an "always" process
    // to allow code re-use, we create fake assignment
    auto comb_process = CombProcess(CombProcess::CombKind::AlwaysComb);
    std::vector<std::unique_ptr<slang::AssignmentExpression>> exprs;
    std::vector<std::unique_ptr<slang::ContinuousAssignSymbol>> stmts;
    std::vector<std::unique_ptr<slang::NamedValueExpression>> names;
    slang::SourceRange sr;
    slang::SourceLocation sl;

    std::set<const slang::Symbol *> sensitivities;

    for (auto const &[port, var] : module->inputs) {
        // inputs is var assigned to port, so it's port = var
        // get the variable symbol given the port name
        auto port_var = module->port_vars.at(port->name);
        auto name = std::make_unique<slang::NamedValueExpression>(*port_var, sr);
        auto expr = std::make_unique<slang::AssignmentExpression>(
            std::nullopt, false, port->getType(), *name, *const_cast<slang::Expression *>(var),
            nullptr, sr);
        auto stmt = std::make_unique<slang::ContinuousAssignSymbol>(sl, *expr);
        names.emplace_back(std::move(name));
        exprs.emplace_back(std::move(expr));
        comb_process.stmts.emplace_back(stmt.get());
        stmts.emplace_back(std::move(stmt));

        // add it to trigger list
        VariableExtractor ex;
        var->visit(ex);
        for (auto const *n : ex.vars) {
            sensitivities.emplace(&n->symbol);
        }
    }

    // for output as well
    for (auto const &[port, var] : module->outputs) {
        // inputs is var assigned to port, so it's var = port
        auto port_var = module->port_vars.at(port->name);
        auto name = std::make_unique<slang::NamedValueExpression>(*port_var, sr);
        auto expr = std::make_unique<slang::AssignmentExpression>(
            std::nullopt, false, *var->type, *const_cast<slang::Expression *>(var), *name, nullptr,
            sr);
        auto stmt = std::make_unique<slang::ContinuousAssignSymbol>(sl, *expr);
        names.emplace_back(std::move(name));
        exprs.emplace_back(std::move(expr));
        comb_process.stmts.emplace_back(stmt.get());
        stmts.emplace_back(std::move(stmt));

        sensitivities.emplace(port_var);
    }

    for (auto const *n : sensitivities) {
        comb_process.sensitive_list.emplace_back(n);
    }

    codegen_always(s, &comb_process, options, info);
}

void output_ctor(std::ostream &s, const Module *module, CodeGenModuleInformation &info) {
    std::set<std::string_view> headers;
    for (auto const &iter : module->child_instances) {
        s << "#include \"" << iter.second->name << ".hh\"" << std::endl;
    }

    // output name space
    s << "namespace fsim {" << std::endl;

    // then output the class ctor
    s << info.get_identifier_name(module->name) << "::" << info.get_identifier_name(module->name)
      << "(): fsim::runtime::Module(\"" << module->name << "\") {" << std::endl;

    for (auto const &[name, m] : module->child_instances) {
        s << name << " = std::make_shared<" << info.get_identifier_name(m->name) << ">();"
          << std::endl;
    }

    // add it to the child instances
    s << fmt::format("child_instances_.reserve({0});", module->child_instances.size()) << std::endl;
    for (auto const &[name, _] : module->child_instances) {
        s << fmt::format("child_instances_.emplace_back({0}.get());", name) << std::endl;
    }

    s << "}" << std::endl;
}

void output_function_header(std::ostream &s, const CXXCodeGenOptions &options,
                            CodeGenModuleInformation &info, const slang::SubroutineSymbol *function,
                            std::string_view prefix) {
    auto const *return_sym = function->returnValVar;
    std::string return_type =
        return_sym ? get_symbol_type(*return_sym, info, options, prefix)
                   : fmt::format("void {0}{1}", prefix, info.get_identifier_name(function->name));

    s << return_type << "(";

    auto const &args = function->getArguments();

    if (function->subroutineKind == slang::SubroutineKind::Task) {
        // process
        s << "fsim::runtime::Process *";
        if (info.has_process()) {
            s << info.current_process_name();
        }
        // then scheduler
        s << ", ";
        s << "fsim::runtime::Scheduler *" << info.scheduler_name();
        if (!args.empty()) s << ", ";
    }

    for (auto i = 0u; i < args.size(); i++) {
        auto const *arg = args[i];
        // TODO:
        //   deal with output arg
        s << get_symbol_type(*arg, info, options);
        if (i != (args.size() - 1)) {
            s << ", ";
        }
    }

    s << ")";
}

void output_function_decl(std::ostream &s, const CXXCodeGenOptions &options,
                          CodeGenModuleInformation &info, const slang::SubroutineSymbol *function) {
    output_function_header(s, options, info, function, {});
    s << ";" << std::endl;
}

void output_function_impl(std::ostream &s, const CXXCodeGenOptions &options,
                          CodeGenModuleInformation &info, const slang::SubroutineSymbol *function,
                          std::string_view name_prefix) {
    bool is_task = function->subroutineKind == slang::SubroutineKind ::Task;
    if (is_task) {
        info.enter_process();
    }
    output_function_header(s, options, info, function, name_prefix);
    s << " {" << std::endl;

    info.current_function = function;
    codegen_sym(s, &function->getBody(), options, info);
    info.current_function = nullptr;

    if (is_task) {
        info.exit_process();
    }

    s << std::endl << "}" << std::endl;
}

void output_header_file(const std::filesystem::path &filename, const Module *mod,
                        const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    // analyze the dependencies to include which headers
    std::stringstream s;
    s << "#pragma once" << std::endl;
    s << raw_header_include;

    // output namespace
    s << "namespace fsim {" << std::endl;

    // forward declaration
    bool has_ctor = !mod->child_instances.empty();
    {
        std::set<std::string_view> class_names;
        for (auto const &iter : mod->child_instances) {
            class_names.emplace(info.get_identifier_name(iter.second->name));
        }
        for (auto const &inst : class_names) {
            s << "class " << inst << ";" << std::endl;
        }
    }

    s << "class " << info.get_identifier_name(mod->name) << ": public fsim::runtime::Module {"
      << std::endl;
    s << "public: " << std::endl;

    // constructor
    if (has_ctor) {
        // if we have ctor, we only generate a signature
        s << info.get_identifier_name(mod->name) << "();" << std::endl;
    } else {
        s << info.get_identifier_name(mod->name) << "(): fsim::runtime::Module(\"" << mod->name
          << "\") {}" << std::endl;
    }

    {
        // start a new pmodule
        info.clear_tracked_names();
        auto const &tracked_vars = mod->get_tracked_vars();
        for (auto const n : tracked_vars) {
            info.add_tracked_name(n);
        }
    }

    // all variables are public. although we can generate private for local variables
    // there is no need to since it's been type checked
    {
        ExprCodeGenVisitor expr_v(s, info);
        VarDeclarationVisitor decl_v(s, options, info, expr_v);
        mod->def()->visit(decl_v);
    }

    // init function
    if (!mod->init_processes.empty()) {
        s << "void init(fsim::runtime::Scheduler *) override;" << std::endl;
    }

    if (!mod->final_processes.empty()) {
        s << "void final(fsim::runtime::Scheduler *) override;" << std::endl;
    }

    if (!mod->comb_processes.empty() || !mod->child_instances.empty()) {
        s << "void comb(fsim::runtime::Scheduler *) override;" << std::endl;
    }

    if (!mod->ff_processes.empty()) {
        s << "void ff(fsim::runtime::Scheduler *) override;" << std::endl;
    }

    // child instances
    for (auto const &[name, inst] : mod->child_instances) {
        // we use shared ptr instead of unique ptr to avoid import the class header
        s << "std::shared_ptr<fsim::" << info.get_identifier_name(inst->name) << "> " << name << ";"
          << std::endl;
    }

    // private information
    { s << "private:" << std::endl; }
    // functions
    for (auto const &func : mod->functions) {
        if (func->is_module_scope()) {
            output_function_decl(s, options, info, &func->subroutine);
        }
    }

    s << "};" << std::endl;

    // namespace
    s << "} // namespace fsim" << std::endl;

    write_to_file(filename.string(), s);
}

void output_cc_file(const std::filesystem::path &filename, const Module *mod,
                    const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    std::stringstream s;
    auto hh_filename = get_hh_filename(mod->name);
    s << "#include \"" << hh_filename << "\"" << std::endl;
    // include more stuff
    s << "#include \"runtime/scheduler.hh\"" << std::endl;
    s << "#include \"runtime/macro.hh\"" << std::endl;

    // vpi
    if (options.add_vpi()) {
        s << "#include \"runtime/vpi.hh\"" << std::endl;
    }

    // dpi
    codegen_dpi_header(mod, s);

    bool has_ctor = !mod->child_instances.empty();
    if (has_ctor) {
        output_ctor(s, mod, info);
    } else {
        // output name space
        s << "namespace fsim {" << std::endl;
    }

    // global functions, which has to be declared first
    for (auto const &func : mod->functions) {
        if (!func->is_module_scope()) {
            output_function_decl(s, options, info, &func->subroutine);
        }
    }

    // initial block
    if (!mod->init_processes.empty()) {
        s << "void " << info.get_identifier_name(mod->name) << "::init(fsim::runtime::Scheduler *"
          << info.scheduler_name() << ") {" << std::endl;

        for (auto const &init : mod->init_processes) {
            codegen_init(s, init.get(), options, info);
        }

        if (!mod->child_instances.empty()) {
            s << "Module::init(scheduler);" << std::endl;
        }

        s << "}" << std::endl;
    }

    // final block
    if (!mod->final_processes.empty()) {
        s << "void " << info.get_identifier_name(mod->name) << "::final(fsim::runtime::Scheduler *"
          << info.scheduler_name() << ") {" << std::endl;

        for (auto const &final : mod->final_processes) {
            codegen_final(s, final.get(), options, info);
        }

        if (!mod->child_instances.empty()) {
            s << "Module::final(scheduler);" << std::endl;
        }

        s << "}" << std::endl;
    }

    // always block
    if (!mod->comb_processes.empty() || !mod->child_instances.empty()) {
        s << "void " << info.get_identifier_name(mod->name) << "::comb(fsim::runtime::Scheduler *"
          << info.scheduler_name() << ") {" << std::endl;

        for (auto const &comb : mod->comb_processes) {
            codegen_always(s, comb.get(), options, info);
        }

        for (auto const &iter : mod->child_instances) {
            codegen_port_connections(s, iter.second.get(), options, info);
        }

        if (!mod->child_instances.empty()) {
            s << "Module::comb(scheduler);" << std::endl;
        }

        s << "}" << std::endl;
    }

    // ff block
    if (!mod->ff_processes.empty()) {
        s << "void " << info.get_identifier_name(mod->name) << "::ff(fsim::runtime::Scheduler *"
          << info.scheduler_name() << ") {" << std::endl;

        for (auto const &comb : mod->ff_processes) {
            codegen_ff(s, comb.get(), options, info);
        }

        if (!mod->child_instances.empty()) {
            s << "Module::ff(scheduler);" << std::endl;
        }

        s << "}" << std::endl;
    }

    // private functions
    auto mod_name_prefix = fmt::format("{0}::", info.get_identifier_name(mod->name));
    for (auto const &func : mod->functions) {
        if (func->is_module_scope()) {
            output_function_impl(s, options, info, &func->subroutine, mod_name_prefix);
        }
    }

    // namespace
    s << "} // namespace fsim" << std::endl;

    write_to_file(filename.string(), s);
}

void output_main_file(const std::string &filename, const Module *top,
                      const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    std::stringstream s;

    s << raw_header_include;

    // include the scheduler
    s << "#include \"runtime/scheduler.hh\"" << std::endl;
    // and macro
    s << "#include \"runtime/macro.hh\"" << std::endl;

    // VPI
    if (options.add_vpi()) {
        s << "#include \"runtime/vpi.hh\"" << std::endl;
    }

    // include the top module file
    s << "#include \"" << top->name << ".hh\"" << std::endl << std::endl;

    // global functions
    auto global_functions = top->get_global_functions();
    if (!global_functions.empty()) {
        // namespace
        s << "namespace fsim {" << std::endl;
        for (auto const *func : global_functions) {
            output_function_impl(s, options, info, func, {});
        }

        s << "} // namespace fsim" << std::endl;
    }

    s << "int main(int argc, char *argv[]) {" << std::endl;

    s << "    fsim::runtime::Scheduler scheduler;" << std::endl
      << "    fsim::" << info.get_identifier_name(top->name) << " top;" << std::endl;

    // vpi
    if (options.add_vpi()) {
        s << "    fsim::runtime::VPIController::get_vpi()->set_args(argc, argv);" << std::endl;
        s << "    scheduler.set_vpi(fsim::runtime::VPIController::get_vpi());" << std::endl;
        s << "    fsim::runtime::VPIController::get_vpi()->set_top(&top);" << std::endl;
        for (auto const &path : options.vpi_libs) {
            s << "    fsim::runtime::VPIController::load(\"" << path << "\");" << std::endl;
        }
    }

    s << "    scheduler.run(&top);" << std::endl << "}";

    write_to_file(filename, s);
}

void CXXCodeGen::output(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto cc_filename = dir_path / get_cc_filename(top_->name);
    auto hh_filename = dir_path / get_hh_filename(top_->name);
    info_.current_module = top_;
    output_header_file(hh_filename, top_, option_, info_);
    output_cc_file(cc_filename, top_, option_, info_);
}

void CXXCodeGen::output_main(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto main_filename = dir_path / fmt::format("{0}.cc", main_name);
    output_main_file(main_filename.string(), top_, option_, info_);
}

}  // namespace fsim