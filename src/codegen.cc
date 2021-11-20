#include "codegen.hh"

#include <filesystem>
#include <fstream>
#include <set>
#include <stack>

#include "fmt/format.h"
#include "slang/binding/SystemSubroutine.h"

namespace xsim {

auto constexpr xsim_delay_event = "xsim_delay_event";
auto constexpr xsim_next_time = "xsim_next_time";

template <typename T>
inline std::string get_cc_filename(const T &name) {
    return fmt::format("{0}.cc", name);
}
template <typename T>
inline std::string get_hh_filename(const T &name) {
    return fmt::format("{0}.hh", name);
}

// need to generate header information about module declaration
// this includes variable, port, and parameter definition

auto constexpr raw_header_include = R"(#include "logic/array.hh"
#include "logic/logic.hh"
#include "logic/struct.hh"
#include "logic/union.hh"
#include "runtime/module.hh"
#include "runtime/system_task.hh"

// forward declaration
namespace xsim::runtime {
class Scheduler;
}

// for logic we use using literal namespace to make codegen simpler
using namespace logic::literals;

)";

std::string_view get_indent(int indent_level) {
    static std::unordered_map<int, std::string> cache;
    if (cache.find(indent_level) == cache.end()) {
        std::stringstream ss;
        for (auto i = 0; i < indent_level; i++) ss << "    ";
        cache.emplace(indent_level, ss.str());
    }
    return cache.at(indent_level);
}

class ExprCodeGenVisitor : public slang::ASTVisitor<ExprCodeGenVisitor, false, true> {
public:
    explicit ExprCodeGenVisitor(std::ostream &s) : s(s) {}
    [[maybe_unused]] void handle(const slang::StringLiteral &str) {
        s << '\"' << str.getValue() << '\"';
    }

    [[maybe_unused]] void handle(const slang::IntegerLiteral &i) {
        auto v = i.getValue();
        auto uint_opt = v.as<int>();
        int value = uint_opt ? *uint_opt : 0;
        s << value;
        // use constexpr
        s << "_logic";
    }

    [[maybe_unused]] void handle(const slang::NamedValueExpression &n) { s << n.symbol.name; }

    [[maybe_unused]] void handle(const slang::ConversionExpression &c) { c.operand().visit(*this); }

    [[maybe_unused]] void handle(const slang::BinaryExpression &expr) {
        // TODO: implement sizing, which is the most bizarre thing compared to C/C++
        auto const &left = expr.left();
        s << "(";
        left.visit(*this);
        bool closing_p = false;
        switch (expr.op) {
            case slang::BinaryOperator::Add:
                s << " + ";
                break;
            case slang::BinaryOperator::ArithmeticShiftLeft:
                s << ".ashl(";
                closing_p = true;
                break;
            case slang::BinaryOperator::ArithmeticShiftRight:
                s << ".ashr(";
                closing_p = true;
                break;
            case slang::BinaryOperator::BinaryAnd:
                s << " & ";
                break;
            case slang::BinaryOperator::BinaryOr:
                s << " | ";
                break;
            case slang::BinaryOperator::BinaryXor:
                s << " ^ ";
                break;
            case slang::BinaryOperator::Equality:
                s << " == ";
                break;
            case slang::BinaryOperator::Inequality:
                s << " != ";
                break;
            default:
                throw std::runtime_error(
                    fmt::format("Unsupported operator {0}", slang::toString(expr.op)));
        }
        auto const &right = expr.right();
        right.visit(*this);
        if (closing_p) s << ")";
        s << ")";
    }

    std::ostream &s;
};

template <bool visit_stmt = true, bool visit_expr = true>
class CodeGenVisitor
    : public slang::ASTVisitor<CodeGenVisitor<visit_stmt, visit_expr>, visit_stmt, visit_expr> {
    // the ultimate visitor
public:
    CodeGenVisitor(std::ostream &s, int &indent_level, const CXXCodeGenOptions &options)
        : s(s), indent_level(indent_level), options(options) {}

    [[maybe_unused]] void handle(const slang::VariableSymbol &var) {
        // output variable definition
        auto const &t = var.getDeclaredType()->getType();
        auto type_name = options.use_4state ? "logic::logic" : "logic::bit";
        auto range = t.getFixedRange();
        s << get_indent(indent_level) << type_name << "<" << range.left << ", " << range.right
          << "> " << var.name << ";" << std::endl;
    }

    [[maybe_unused]] void handle(const slang::NetSymbol &var) {
        // output variable definition
        auto const &t = var.getDeclaredType()->getType();
        auto type_name = options.use_4state ? "logic::logic" : "logic::bit";
        auto range = t.getFixedRange();
        s << get_indent(indent_level) << type_name << "<" << range.left << ", " << range.right
          << "> " << var.name << ";" << std::endl;
    }

    [[maybe_unused]] void handle(const slang::VariableDeclStatement &stmt) {
        s << std::endl;
        auto const &v = stmt.symbol;
        handle(v);
    }

    [[maybe_unused]] void handle(const slang::TimedStatement &stmt) {
        auto const &timing = stmt.timing;
        if (timing.kind != slang::TimingControlKind::Delay) {
            throw std::runtime_error("Only delay timing control supported");
        }
        auto const &delay = timing.as<slang::DelayControl>();
        s << std::endl
          << get_indent(indent_level) << xsim_next_time << ".time = scheduler->sim_time + (";
        ExprCodeGenVisitor v(s);
        delay.expr.visit(v);
        s << ").to_uint64();" << std::endl;
        s << get_indent(indent_level) << "scheduler->schedule_delay(&" << xsim_next_time << ");"
          << std::endl;
        s << get_indent(indent_level) << "init_ptr->cond.clear();" << std::endl;
        s << get_indent(indent_level) << xsim_delay_event << ".wait();" << std::endl;
        this->template visitDefault(stmt.stmt);
    }

    [[maybe_unused]] void handle(const slang::AssignmentExpression &expr) {
        auto const &left = expr.left();
        ExprCodeGenVisitor v(s);
        left.visit(v);
        s << " = ";
        auto const &right = expr.right();
        right.visit(v);
    }

    [[maybe_unused]] void handle(const slang::StatementBlockSymbol &) {
        // we ignore this one for now
    }

    [[maybe_unused]] void handle(const slang::StatementList &list) {
        // entering a scope
        s << "{";
        indent_level++;
        this->template visitDefault(list);
        indent_level--;
        s << get_indent(indent_level) << "}" << std::endl;
    }

    [[maybe_unused]] void handle(const slang::ExpressionStatement &stmt) {
        s << std::endl << get_indent(indent_level);
        this->template visitDefault(stmt);
        s << ";" << std::endl;
    }

    [[maybe_unused]] void handle(const slang::ConditionalStatement &stmt) {
        s << get_indent(indent_level);
        auto const &cond = stmt.cond;
        s << "if (";
        ExprCodeGenVisitor v(s);
        cond.visit(v);
        s << ")";
        stmt.ifTrue.visit(*this);
        if (stmt.ifFalse) {
            s << get_indent(indent_level) << "else ";
            stmt.ifFalse->visit(*this);
        }
    }

    [[maybe_unused]] void handle(const slang::CallExpression &expr) {
        if (expr.subroutine.index() == 1) {
            auto const &info = std::get<1>(expr.subroutine);
            auto name = info.subroutine->name;
            // remove the leading $
            auto func_name = fmt::format("xsim::runtime::{0}", name.substr(1));
            s << func_name << "(this, ";
            auto const &arguments = expr.arguments();
            for (auto i = 0u; i < arguments.size(); i++) {
                auto const *arg = arguments[i];
                ExprCodeGenVisitor v(s);
                arg->visit(v);
                if (i != (arguments.size() - 1)) {
                    s << ", ";
                }
            }
            s << ")";
        } else {
            const auto &symbol = *std::get<0>(expr.subroutine);
            throw std::runtime_error(
                fmt::format("Not yet implemented for symbol {0}", symbol.name));
        }
    }

private:
    std::ostream &s;
    int &indent_level;
    const CXXCodeGenOptions &options;
};

void codegen_sym(std::ostream &s, int &indent_level, const slang::Symbol *sym,
                 const CXXCodeGenOptions &options) {
    CodeGenVisitor v(s, indent_level, options);
    sym->visit(v);
}

void codegen_init(std::ostream &s, int &indent_level, const Process *process,
                  const CXXCodeGenOptions &options) {
    s << get_indent(indent_level) << "{" << std::endl;
    indent_level++;

    // TODO:
    //     need to compute the number of delay controls needed
    //     for now we only create one and use C++ shadowing to deal with it
    s << get_indent(indent_level) << "auto " << xsim_delay_event
      << " = marl::Event(marl::Event::Mode::Manual);" << std::endl;
    s << get_indent(indent_level) << "auto " << xsim_next_time
      << " = xsim::runtime::ScheduledTimeslot(0, " << xsim_delay_event << ");" << std::endl;

    s << get_indent(indent_level)
      << "auto init_ptr = std::make_shared<xsim::runtime::InitialProcess>();" << std::endl
      << get_indent(indent_level) << "init_ptr->func = [this, init_ptr, " << '&' << xsim_next_time
      << ", " << '&' << xsim_delay_event << ", scheduler]() {" << std::endl;
    indent_level++;
    s << get_indent(indent_level);

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, indent_level, stmt, options);
    }

    s << get_indent(indent_level) << "init_ptr->finished = true;" << std::endl
      << get_indent(indent_level) << "init_ptr->cond.signal();" << std::endl;
    indent_level--;
    s << get_indent(indent_level) << "};" << std::endl;
    s << get_indent(indent_level) << "scheduler->schedule_init(init_ptr);" << std::endl;

    indent_level--;
    s << get_indent(indent_level) << "}" << std::endl;
}

void output_header_file(const std::filesystem::path &filename, const Module *mod,
                        const CXXCodeGenOptions &options) {
    // analyze the dependencies to include which headers
    std::ofstream s(filename, std::ios::trunc);
    int indent_level = 0;
    s << "#pragma once" << std::endl;
    s << raw_header_include;

    s << get_indent(indent_level) << "class " << mod->name << ": public xsim::runtime::Module {"
      << std::endl;
    s << get_indent(indent_level) << "public: " << std::endl;

    indent_level++;
    // constructor
    s << get_indent(indent_level) << mod->name << "(): xsim::runtime::Module(\"" << mod->name
      << "\") {}" << std::endl;

    // all variables are public
    {
        CodeGenVisitor<false, false> v(s, indent_level, options);
        mod->def()->visit(v);
    }

    // init function
    if (!mod->init_processes.empty()) {
        s << get_indent(indent_level) << "void init(xsim::runtime::Scheduler *scheduler) override;"
          << std::endl;
    }

    indent_level--;

    s << get_indent(indent_level) << "};";
}

void output_cc_file(const std::filesystem::path &filename, const Module *mod,
                    const CXXCodeGenOptions &options) {
    std::ofstream s(filename, std::ios::trunc);
    auto hh_filename = get_hh_filename(mod->name);
    s << "#include \"" << hh_filename << "\"" << std::endl;
    // include more stuff
    s << "#include \"runtime/scheduler.hh\"" << std::endl;

    int indent_level = 0;

    if (!mod->init_processes.empty()) {
        s << get_indent(indent_level) << "void " << mod->name
          << "::init(xsim::runtime::Scheduler *scheduler) {" << std::endl;
        indent_level++;

        for (auto const &init : mod->init_processes) {
            codegen_init(s, indent_level, init.get(), options);
        }

        indent_level--;
        s << get_indent(indent_level) << "}";
    }
    // compute init fiber conditional variables. each init process will have one variable,
    // which will be signalled if the init process finishes
}

void output_main_file(const std::string &filename, const Module *top) {
    std::ofstream s(filename, std::ios::trunc);

    s << raw_header_include;

    // include the scheduler
    s << "#include \"runtime/scheduler.hh\"" << std::endl;
    // include the top module file
    s << "#include \"" << top->name << ".hh\"" << std::endl << std::endl;
    s << "int main(int argc, char *argv[]) {" << std::endl;

    s << "    xsim::runtime::Scheduler scheduler;" << std::endl
      << "    " << top->name << " top;" << std::endl
      << "    scheduler.run(&top);" << std::endl
      << "}";
}

void CXXCodeGen::output(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto cc_filename = dir_path / get_cc_filename(top_->name);
    auto hh_filename = dir_path / get_hh_filename(top_->name);
    output_header_file(hh_filename, top_, option_);
    output_cc_file(cc_filename, top_, option_);
    auto main_filename = dir_path / main_name;
    output_main_file(main_filename, top_);
}

void get_defs(const Module *module, std::set<std::string> &result) {
    result.emplace(module->name);
    for (auto const &[_, inst] : module->child_instances) {
        get_defs(inst.get(), result);
    }
}

std::set<std::string> get_defs(const Module *module) {
    std::set<std::string> result;
    get_defs(module, result);
    return result;
}

void NinjaCodeGen::output(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto ninja_filename = dir_path / "build.ninja";
    std::ofstream stream(ninja_filename, std::ios::trunc);
    // filling out missing information
    // use the output dir as the runtime dir
    if (options_.clang_path.empty()) {
        // hope for the best?
        auto const *cxx = std::getenv("XSIM_CXX");
        if (cxx) {
            options_.clang_path = cxx;
        } else {
            options_.clang_path = "clang";
        }
    }
    if (options_.binary_name.empty()) {
        options_.binary_name = default_output_name;
    }

    std::filesystem::path runtime_dir = std::filesystem::absolute(dir);
    auto include_dir = runtime_dir / "include";
    auto lib_path = runtime_dir / "lib";
    auto runtime_lib_path = lib_path / "libxsim-runtime.so";
    stream << "cflags = -I" << include_dir << " -std=c++20 ";
    if (options_.debug_build) {
        stream << "-O0 -g ";
    } else {
        stream << "-O3";
    }
    stream << std::endl << std::endl;
    stream << "rule cc" << std::endl;
    stream << "  depfile = $out.d" << std::endl;
    stream << "  command = " << options_.clang_path << " -MD -MF $out.d $cflags -c $in -o $out"
           << std::endl
           << std::endl;

    // output for each module definition
    auto defs = get_defs(top_);
    std::string objs;
    for (auto const &name : defs) {
        auto obj_name = fmt::format("{0}.o", name);
        stream << "build " << obj_name << ": cc " << get_cc_filename(name) << std::endl;
        objs.append(obj_name).append(" ");
    }
    auto main_linkers = fmt::format("-pthread -lstdc++ -Wl,-rpath,{0}", lib_path.string());
    // build the main
    stream << "rule main" << std::endl;
    stream << "  command = " << options_.clang_path << " $in " << runtime_lib_path << " $cflags "
           << main_linkers << " -o $out" << std::endl
           << std::endl;
    stream << "build " << options_.binary_name << ": main " << main_name << " " << objs
           << std::endl;
}

}  // namespace xsim