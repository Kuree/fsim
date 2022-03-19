#include "cxx.hh"

#include <filesystem>
#include <set>

#include "../ir/ast.hh"
#include "dpi.hh"
#include "slang/symbols/VariableSymbols.h"
#include "stmt.hh"
#include "util.hh"

namespace xsim {

auto constexpr xsim_end_process = "END_PROCESS";

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
namespace xsim::runtime {
class Scheduler;
}

// for logic we use using literal namespace to make codegen simpler
using namespace logic::literals;

)";

void codegen_sym(std::ostream &s, int &indent_level, const slang::Symbol *sym,
                 const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    StmtCodeGenVisitor v(s, indent_level, options, info);
    sym->visit(v);
}

void codegen_edge_control(std::ostream &s, int &indent_level, const Process *process,
                          CodeGenModuleInformation &info) {
    ExprCodeGenVisitor v(s, info);
    if (!process->edge_event_controls.empty()) {
        slang::SourceRange sr;
        for (auto const &[var, _] : process->edge_event_controls) {
            s << get_indent(indent_level);
            auto name = slang::NamedValueExpression(*var, sr);
            name.visit(v);
            s << ".track_edge = true;" << std::endl;

            s << get_indent(indent_level) << info.scheduler_name() << "->add_tracked_var(&";
            name.visit(v);
            s << ");" << std::endl;
        }
        s << get_indent(indent_level)
          << fmt::format("scheduler->add_process_edge_control({0});", info.current_process_name())
          << std::endl;
    }
}

void codegen_init(std::ostream &s, int &indent_level, const Process *process,
                  const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    s << get_indent(indent_level) << "{" << std::endl;
    indent_level++;

    auto const &ptr_name = info.enter_process();
    s << get_indent(indent_level)
      << fmt::format("auto {0} = {1}->create_init_process();", ptr_name, info.scheduler_name())
      << std::endl
      << get_indent(indent_level)
      << fmt::format("{0}->func = [this, {0}, {1}]() {{", ptr_name, info.scheduler_name())
      << std::endl;
    indent_level++;

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, indent_level, stmt, options, info);
    }

    s << get_indent(indent_level) << xsim_end_process << "(" << ptr_name << ");" << std::endl;
    indent_level--;
    s << get_indent(indent_level) << "};" << std::endl;
    s << get_indent(indent_level)
      << fmt::format("xsim::runtime::Scheduler::schedule_init({0});", ptr_name) << std::endl;
    s << get_indent(indent_level) << fmt::format("init_processes_.emplace_back({0});", ptr_name)
      << std::endl;

    codegen_edge_control(s, indent_level, process, info);

    indent_level--;
    info.exit_process();
    s << get_indent(indent_level) << "}" << std::endl;
}

void codegen_final(std::ostream &s, int &indent_level, const Process *process,
                   const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    s << get_indent(indent_level) << "{" << std::endl;
    indent_level++;
    auto const &ptr_name = info.enter_process();

    s << get_indent(indent_level)
      << fmt::format("auto {0} = {1}->create_final_process();", ptr_name, info.scheduler_name())
      << std::endl
      << get_indent(indent_level)
      << fmt::format("{0}->func = [this, {0}, {1}]() {{", ptr_name, info.scheduler_name())
      << std::endl;
    indent_level++;

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, indent_level, stmt, options, info);
    }

    indent_level--;
    s << get_indent(indent_level) << "};" << std::endl;
    s << get_indent(indent_level)
      << fmt::format("xsim::runtime::Scheduler::schedule_final({0});", ptr_name) << std::endl;

    indent_level--;
    info.exit_process();
    s << get_indent(indent_level) << "}" << std::endl;
}

void codegen_always(std::ostream &s, int &indent_level, const CombProcess *process,
                    const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    s << get_indent(indent_level) << "{" << std::endl;
    indent_level++;
    auto const &ptr_name = info.enter_process();

    // depends on the comb process type, we may generate different style
    bool infinite_loop = process->kind == CombProcess::CombKind::GeneralPurpose;
    // declare the always block

    s << get_indent(indent_level)
      << fmt::format("auto {0} = {1}->create_comb_process();", ptr_name, info.scheduler_name())
      << std::endl
      << get_indent(indent_level)
      << fmt::format("{0}->func = [this, {0}, {1}]() {{", ptr_name, info.scheduler_name())
      << std::endl;
    indent_level++;

    if (infinite_loop) {
        s << get_indent(indent_level) << "while (true) {" << std::endl;
        indent_level++;
    }

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, indent_level, stmt, options, info);
    }

    // general purpose always doesn't have end process since it never ends
    if (!infinite_loop)
        s << get_indent(indent_level) << xsim_end_process << "(" << ptr_name << ");" << std::endl;

    if (infinite_loop) {
        indent_level--;
        s << get_indent(indent_level) << "}" << std::endl;
    }

    indent_level--;
    s << get_indent(indent_level) << "};" << std::endl;

    // set input changed
    for (auto *var : process->sensitive_list) {
        s << get_indent(indent_level);
        ExprCodeGenVisitor v(s, info);
        var->visit(v);
        s << ".comb_processes.emplace_back(" << ptr_name << ");" << std::endl;
    }

    s << get_indent(indent_level) << fmt::format("comb_processes_.emplace_back({0});", ptr_name)
      << std::endl;

    codegen_edge_control(s, indent_level, process, info);

    indent_level--;
    info.exit_process();
    s << get_indent(indent_level) << "}" << std::endl;
}

void codegen_ff(std::ostream &s, int &indent_level, const FFProcess *process,
                const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    s << get_indent(indent_level) << "{" << std::endl;
    indent_level++;
    auto const &ptr_name = info.enter_process();

    s << get_indent(indent_level)
      << fmt::format("auto {0} = {1}->create_ff_process();", ptr_name, info.scheduler_name())
      << std::endl
      << get_indent(indent_level)
      << fmt::format("{0}->func = [this, {0}, {1}]() {{", ptr_name, info.scheduler_name())
      << std::endl;
    indent_level++;

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, indent_level, stmt, options, info);
    }

    // output end process
    s << get_indent(indent_level) << xsim_end_process << "(" << ptr_name << ");" << std::endl;

    indent_level--;
    s << get_indent(indent_level) << "};" << std::endl;

    s << get_indent(indent_level) << fmt::format("ff_process_.emplace_back({0});", ptr_name)
      << std::endl;

    // generate edge trigger functions
    for (auto const &[edge, v] : process->edges) {
        if (edge == slang::EdgeKind::PosEdge || edge == slang::EdgeKind::BothEdges) {
            s << get_indent(indent_level) << v->name << ".ff_posedge_processes.emplace_back("
              << ptr_name << ");" << std::endl;
        }
        if (edge == slang::EdgeKind::NegEdge || edge == slang::EdgeKind::BothEdges) {
            s << get_indent(indent_level) << v->name << ".ff_negedge_processes.emplace_back("
              << ptr_name << ");" << std::endl;
        }
    }

    // set edge tracking as well
    std::set<std::string_view> vars;
    for (auto const &iter : process->edges) {
        vars.emplace(iter.second->name);
    }
    for (auto const &name : vars) {
        s << get_indent(indent_level) << name << ".track_edge = true;" << std::endl;
    }

    codegen_edge_control(s, indent_level, process, info);

    indent_level--;
    info.exit_process();
    s << get_indent(indent_level) << "}" << std::endl;
}

void codegen_port_connections(std::ostream &s, int &indent_level, const Module *module,
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

    codegen_always(s, indent_level, &comb_process, options, info);
}

void output_ctor(std::ostream &s, int &indent_level, const Module *module) {
    std::set<std::string_view> headers;
    for (auto const &iter : module->child_instances) {
        s << "#include \"" << iter.second->name << ".hh\"" << std::endl;
    }

    // output name space
    s << "namespace xsim {" << std::endl;

    // then output the class ctor
    s << module->name << "::" << module->name << "(): xsim::runtime::Module(\"" << module->name
      << "\") {" << std::endl;
    indent_level++;

    for (auto const &[name, m] : module->child_instances) {
        s << get_indent(indent_level) << name << " = std::make_shared<" << m->name << ">();"
          << std::endl;
    }

    // add it to the child instances
    s << get_indent(indent_level)
      << fmt::format("child_instances_.reserve({0});", module->child_instances.size()) << std::endl;
    for (auto const &[name, _] : module->child_instances) {
        s << get_indent(indent_level)
          << fmt::format("child_instances_.emplace_back({0}.get());", name) << std::endl;
    }

    indent_level--;
    s << "}" << std::endl;
}

void output_header_file(const std::filesystem::path &filename, const Module *mod,
                        const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    // analyze the dependencies to include which headers
    std::stringstream s;
    int indent_level = 0;
    s << "#pragma once" << std::endl;
    s << raw_header_include;

    // output namespace
    s << "namespace xsim {" << std::endl;

    // forward declaration
    bool has_ctor = !mod->child_instances.empty();
    {
        std::set<std::string_view> class_names;
        for (auto const &iter : mod->child_instances) {
            class_names.emplace(iter.second->name);
        }
        for (auto const &inst : class_names) {
            s << get_indent(indent_level) << "class " << inst << ";" << std::endl;
        }
    }

    s << get_indent(indent_level) << "class " << mod->name << ": public xsim::runtime::Module {"
      << std::endl;
    s << get_indent(indent_level) << "public: " << std::endl;

    indent_level++;
    // constructor
    if (has_ctor) {
        // if we have ctor, we only generate a signature
        s << get_indent(indent_level) << mod->name << "();" << std::endl;
    } else {
        s << get_indent(indent_level) << mod->name << "(): xsim::runtime::Module(\"" << mod->name
          << "\") {}" << std::endl;
    }

    {
        auto const &tracked_vars = mod->get_tracked_vars();
        for (auto const n : tracked_vars) {
            info.add_tracked_name(n);
        }
    }

    // all variables are public
    {
        ExprCodeGenVisitor expr_v(s, info);
        VarDeclarationVisitor decl_v(s, indent_level, options, info, expr_v);
        mod->def()->visit(decl_v);
    }

    // init function
    if (!mod->init_processes.empty()) {
        s << get_indent(indent_level) << "void init(xsim::runtime::Scheduler *) override;"
          << std::endl;
    }

    if (!mod->final_processes.empty()) {
        s << get_indent(indent_level) << "void final(xsim::runtime::Scheduler *) override;"
          << std::endl;
    }

    if (!mod->comb_processes.empty() || !mod->child_instances.empty()) {
        s << get_indent(indent_level) << "void comb(xsim::runtime::Scheduler *) override;"
          << std::endl;
    }

    if (!mod->ff_processes.empty()) {
        s << get_indent(indent_level) << "void ff(xsim::runtime::Scheduler *) override;"
          << std::endl;
    }

    // child instances
    for (auto const &[name, inst] : mod->child_instances) {
        // we use shared ptr instead of unique ptr to avoid import the class header
        s << get_indent(indent_level) << "std::shared_ptr<xsim::" << inst->name << "> " << name
          << ";" << std::endl;
    }

    indent_level--;

    s << get_indent(indent_level) << "};" << std::endl;

    // namespace
    s << "} // namespace xsim" << std::endl;

    write_to_file(filename, s);
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

    int indent_level = 0;

    // dpi
    codegen_dpi_header(mod, s, indent_level);

    bool has_ctor = !mod->child_instances.empty();
    if (has_ctor) {
        output_ctor(s, indent_level, mod);
    } else {
        // output name space
        s << "namespace xsim {" << std::endl;
    }

    // initial block
    if (!mod->init_processes.empty()) {
        s << get_indent(indent_level) << "void " << mod->name << "::init(xsim::runtime::Scheduler *"
          << info.scheduler_name() << ") {" << std::endl;
        indent_level++;

        for (auto const &init : mod->init_processes) {
            codegen_init(s, indent_level, init.get(), options, info);
        }

        if (!mod->child_instances.empty()) {
            s << get_indent(indent_level) << "Module::init(scheduler);" << std::endl;
        }

        indent_level--;
        s << get_indent(indent_level) << "}" << std::endl;
    }

    // final block
    if (!mod->final_processes.empty()) {
        s << get_indent(indent_level) << "void " << mod->name
          << "::final(xsim::runtime::Scheduler *" << info.scheduler_name() << ") {" << std::endl;
        indent_level++;

        for (auto const &final : mod->final_processes) {
            codegen_final(s, indent_level, final.get(), options, info);
        }

        if (!mod->child_instances.empty()) {
            s << get_indent(indent_level) << "Module::final(scheduler);" << std::endl;
        }

        indent_level--;
        s << get_indent(indent_level) << "}" << std::endl;
    }

    // always block
    if (!mod->comb_processes.empty() || !mod->child_instances.empty()) {
        s << get_indent(indent_level) << "void " << mod->name << "::comb(xsim::runtime::Scheduler *"
          << info.scheduler_name() << ") {" << std::endl;
        indent_level++;

        for (auto const &comb : mod->comb_processes) {
            codegen_always(s, indent_level, comb.get(), options, info);
        }

        for (auto const &iter : mod->child_instances) {
            codegen_port_connections(s, indent_level, iter.second.get(), options, info);
        }

        if (!mod->child_instances.empty()) {
            s << get_indent(indent_level) << "Module::comb(scheduler);" << std::endl;
        }

        indent_level--;
        s << get_indent(indent_level) << "}" << std::endl;
    }

    // ff block
    if (!mod->ff_processes.empty()) {
        s << get_indent(indent_level) << "void " << mod->name << "::ff(xsim::runtime::Scheduler *"
          << info.scheduler_name() << ") {" << std::endl;
        indent_level++;

        for (auto const &comb : mod->ff_processes) {
            codegen_ff(s, indent_level, comb.get(), options, info);
        }

        if (!mod->child_instances.empty()) {
            s << get_indent(indent_level) << "Module::ff(scheduler);" << std::endl;
        }

        indent_level--;
        s << get_indent(indent_level) << "}" << std::endl;
    }

    // namespace
    s << "} // namespace xsim" << std::endl;

    write_to_file(filename, s);
}

void output_main_file(const std::string &filename, const Module *top,
                      const CXXCodeGenOptions &options) {
    std::stringstream s;

    s << raw_header_include;

    // include the scheduler
    s << "#include \"runtime/scheduler.hh\"" << std::endl;

    // VPI
    if (options.add_vpi()) {
        s << "#include \"runtime/vpi.hh\"" << std::endl;
    }

    // include the top module file
    s << "#include \"" << top->name << ".hh\"" << std::endl << std::endl;
    s << "int main(int argc, char *argv[]) {" << std::endl;

    s << "    xsim::runtime::Scheduler scheduler;" << std::endl
      << "    xsim::" << top->name << " top;" << std::endl;

    // vpi
    if (options.add_vpi()) {
        s << "xsim::runtime::VPIController::get_vpi()->set_args(argc, argv);" << std::endl;
        s << "scheduler.set_vpi(xsim::runtime::VPIController::get_vpi());" << std::endl;
        s << "xsim::runtime::VPIController::get_vpi()->set_top(&top);" << std::endl;
        for (auto const &path : options.vpi_libs) {
            s << "xsim::runtime::VPIController::load(\"" << path << "\");" << std::endl;
        }
    }

    s << "    scheduler.run(&top);" << std::endl << "}";

    write_to_file(filename, s);
}

void CXXCodeGen::output(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto cc_filename = dir_path / get_cc_filename(top_->name);
    auto hh_filename = dir_path / get_hh_filename(top_->name);
    CodeGenModuleInformation info;
    info.current_module = top_;
    output_header_file(hh_filename, top_, option_, info);
    output_cc_file(cc_filename, top_, option_, info);
}

void CXXCodeGen::output_main(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto main_filename = dir_path / fmt::format("{0}.cc", main_name);
    output_main_file(main_filename, top_, option_);
}

}  // namespace xsim