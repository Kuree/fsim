#include "codegen.hh"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stack>
#include <unordered_set>

#include "fmt/format.h"
#include "slang/binding/SystemSubroutine.h"

namespace xsim {
auto constexpr xsim_next_time = "xsim_next_time";
auto constexpr xsim_schedule_delay = "SCHEDULE_DELAY";
auto constexpr xsim_schedule_nba = "SCHEDULE_NBA";

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

class CodeGenModuleInformation {
public:
    // used to hold different information while passing between different visitors

    std::string get_new_name(const std::string &prefix, bool track_new_name = true) {
        std::string suffix;
        uint64_t count = 0;
        while (true) {
            auto result = fmt::format("{0}{1}", prefix, suffix);
            if (used_names_.find(result) == used_names_.end()) {
                if (track_new_name) {
                    used_names_.emplace(result);
                }
                return result;
            }
            suffix = std::to_string(count++);
        }
    }

    [[maybe_unused]] inline void add_used_names(std::string_view name) {
        used_names_.emplace(std::string(name));
    }

    std::string enter_process() {
        auto name = get_new_name("process");
        process_names_.emplace(name);
        return name;
    }

    std::string current_process_name() const { return process_names_.top(); }
    const std::string &scheduler_name() {
        if (scheduler_name_.empty()) {
            scheduler_name_ = get_new_name("scheduler");
        }
        return scheduler_name_;
    }

    [[nodiscard]] bool var_tracked(std::string_view name) const {
        return tracked_vars_.find(name) != tracked_vars_.end();
    }
    void add_tracked_name(std::string_view name) { tracked_vars_.emplace(name); }

    void exit_process() {
        auto name = current_process_name();
        // before we create a new scope for every process, when it exists the scope, we can
        // recycle the process name
        used_names_.erase(name);
        process_names_.pop();
    }

private:
    std::stack<std::string> process_names_;
    std::unordered_set<std::string> used_names_;
    std::unordered_set<std::string_view> tracked_vars_;

    std::string scheduler_name_;
};

void write_to_file(const std::string &filename, std::stringstream &stream) {
    if (!std::filesystem::exists(filename)) {
        // doesn't exist, directly write to the file
        std::ofstream s(filename, std::ios::trunc);
        s << stream.rdbuf();
    } else {
        // need to compare size
        // if they are different, output to that file
        {
            std::ifstream f(filename, std::ifstream::ate | std::ifstream::binary);
            auto size = static_cast<uint64_t>(f.tellg());
            auto buf = stream.str();
            if (size != buf.size()) {
                std::ofstream s(filename, std::ios::trunc);
                s << buf;
            } else {
                // they are the same size, now need to compare its actual content
                std::string contents;
                f.seekg(0, std::ios::end);
                contents.resize(f.tellg());
                f.seekg(0, std::ios::beg);
                f.read(&contents[0], static_cast<int64_t>(contents.size()));

                if (contents != buf) {
                    f.close();
                    std::ofstream s(filename, std::ios::trunc);
                    s << buf;
                }
            }
        }
    }
}

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
        s << "_logic";
    }

    [[maybe_unused]] void handle(const slang::NamedValueExpression &n) { s << n.symbol.name; }

    [[maybe_unused]] void handle(const slang::ConversionExpression &c) {
        auto const &t = *c.type;
        s << "(";
        c.operand().visit(*this);
        s << ")";
        if (c.operand().kind != slang::ExpressionKind::IntegerLiteral) {
            if (c.operand().type->getBitWidth() > t.getBitWidth()) {
                // this is a slice
                s << ".slice<" << t.getFixedRange().left << ", " << t.getFixedRange().right
                  << ">()";
            } else if (c.operand().type->getBitWidth() < t.getBitWidth()) {
                // it's an extension
                auto size = std::abs(t.getFixedRange().left - t.getFixedRange().right);
                s << ".extend<" << size << ">()";
            }
            if (c.operand().type->isSigned() && !t.isSigned()) {
                s << ".to_unsigned()";
            } else if (!c.operand().type->isSigned() && t.isSigned()) {
                s << ".to_signed()";
            }
        }
    }

    [[maybe_unused]] void handle(const slang::LValueReferenceExpression &) {
        if (!left_ptr) throw std::runtime_error("Unable to determine LValue");
        left_ptr->visit(*this);
    }

    [[maybe_unused]] void handle(const slang::UnaryExpression &expr) {
        auto const &op = expr.operand();
        s << "(";
        // simple ones that have C++ operator overloaded
        bool handled = true;
        switch (expr.op) {
            case slang::UnaryOperator::Minus:
                s << "-";
                op.visit(*this);
                break;
            case slang::UnaryOperator::Plus:
                s << "+";
                op.visit(*this);
                break;
            case slang::UnaryOperator::Predecrement:
                s << "--";
                op.visit(*this);
                break;
            case slang::UnaryOperator::Preincrement:
                s << "++";
                op.visit(*this);
                break;
            default:
                handled = false;
        }
        // complex one that uses special function calls
        switch (expr.op) {
            case slang::UnaryOperator::BitwiseAnd:
                op.visit(*this);
                s << ").r_and(";
                break;
            case slang::UnaryOperator::BitwiseOr:
                op.visit(*this);
                s << ").r_or(";
                break;
            case slang::UnaryOperator::Preincrement:
                s << "++";
                op.visit(*this);
                break;
            case slang::UnaryOperator::Predecrement:
                s << "--";
                op.visit(*this);
                break;
            case slang::UnaryOperator::Postdecrement:
                op.visit(*this);
                s << "--";
                break;
            case slang::UnaryOperator::Postincrement:
                op.visit(*this);
                s << "++";
                break;
            default:
                if (!handled)
                    throw std::runtime_error(
                        fmt::format("Unsupported operator {0}", slang::toString(expr.op)));
        }
        // the one with special function calls
        s << ")";
    }

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
            case slang::BinaryOperator::LogicalAnd:
                s << " && ";
                break;
            case slang::BinaryOperator::LogicalOr:
                s << " || ";
                break;
            case slang::BinaryOperator ::LessThan:
                s << " < ";
                break;
            case slang::BinaryOperator::LessThanEqual:
                s << " <= ";
                break;
            case slang::BinaryOperator::GreaterThan:
                s << " > ";
                break;
            case slang::BinaryOperator::GreaterThanEqual:
                s << " >= ";
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

    const slang::Expression *left_ptr = nullptr;
};

template <bool visit_stmt = true, bool visit_expr = true>
class CodeGenVisitor
    : public slang::ASTVisitor<CodeGenVisitor<visit_stmt, visit_expr>, visit_stmt, visit_expr> {
    // the ultimate visitor
public:
    CodeGenVisitor(std::ostream &s, int &indent_level, const CXXCodeGenOptions &options,
                   CodeGenModuleInformation &module_info)
        : s(s), indent_level(indent_level), options(options), module_info(module_info) {}

    [[maybe_unused]] void handle(const slang::VariableSymbol &var) {
        // output variable definition
        auto const &t = var.getDeclaredType()->getType();
        auto type_name = get_var_type(var.name);
        auto range = t.getFixedRange();
        s << get_indent(indent_level) << type_name << "<" << range.left << ", " << range.right
          << "> " << var.name;

        auto *init = var.getInitializer();
        if (init) {
            s << " = ";
            ExprCodeGenVisitor v(s);
            init->visit(v);
        }

        s << ";" << std::endl;
        // add it to tracked names
        module_info.add_used_names(var.name);
    }

    [[maybe_unused]] void handle(const slang::NetSymbol &var) {
        // output variable definition
        auto const &t = var.getDeclaredType()->getType();
        auto type_name = get_var_type(var.name);
        auto range = t.getFixedRange();
        s << get_indent(indent_level) << type_name << "<" << range.left << ", " << range.right
          << "> " << var.name << ";" << std::endl;
        module_info.add_used_names(var.name);
    }

    [[maybe_unused]] void handle(const slang::VariableDeclStatement &stmt) {
        s << std::endl;
        auto const &v = stmt.symbol;
        handle(v);
    }

    [[maybe_unused]] void handle(const slang::TimedStatement &stmt) {
        auto const &timing = stmt.timing;
        if (timing.kind != slang::TimingControlKind::Delay) {
            // only care about delay so far
            stmt.stmt.visit(*this);
            return;
        }
        s << std::endl;
        // we first release the current condition holds

        auto const &delay = timing.as<slang::DelayControl>();

        s << get_indent(indent_level)
          << fmt::format("{0}({1}, (", xsim_schedule_delay, module_info.current_process_name());
        ExprCodeGenVisitor v(s);
        delay.expr.visit(v);
        s << fmt::format(").to_uint64(), {0}, {1});", module_info.scheduler_name(),
                         module_info.get_new_name(xsim_next_time, false));

        stmt.stmt.visit(*this);
    }

    [[maybe_unused]] void handle(const slang::AssignmentExpression &expr) {
        if (expr.isNonBlocking()) {
            s << xsim_schedule_nba << "(";
            auto const &left = expr.left();
            ExprCodeGenVisitor v(s);
            left.visit(v);
            s << ", ";
            auto const &right = expr.right();
            right.visit(v);
            s << ", " << module_info.current_process_name() << ")";
        } else {
            auto const &left = expr.left();
            ExprCodeGenVisitor v(s);
            left.visit(v);
            v.left_ptr = &left;
            s << " = ";
            auto const &right = expr.right();
            right.visit(v);
        }
    }

    [[maybe_unused]] void handle(const slang::StatementBlockSymbol &) {
        // we ignore this one for now
    }

    [[maybe_unused]] void handle(const slang::StatementList &list) {
        // entering a scope
        s << get_indent(indent_level) << "{";
        indent_level++;
        this->template visitDefault(list);
        indent_level--;
        s << std::endl << get_indent(indent_level) << "}" << std::endl;
    }

    [[maybe_unused]] void handle(const slang::ExpressionStatement &stmt) {
        s << std::endl << get_indent(indent_level);
        this->template visitDefault(stmt);
        s << ";";
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
            // remove the leading $
            auto name = info.subroutine->name.substr(1);
            auto func_name = fmt::format("xsim::runtime::{0}", name);
            s << func_name << "(";
            // depends on the context, we may or may not insert additional arguments
            if (name == "finish") {
                s << module_info.scheduler_name();
            } else {
                s << "this";
            }

            auto const &arguments = expr.arguments();
            for (auto const &arg : arguments) {
                s << ", ";
                ExprCodeGenVisitor v(s);
                arg->visit(v);
            }
            s << ")";
        } else {
            const auto &symbol = *std::get<0>(expr.subroutine);
            throw std::runtime_error(
                fmt::format("Not yet implemented for symbol {0}", symbol.name));
        }
    }

    [[maybe_unused]] void handle(const slang::ContinuousAssignSymbol &sym) {
        if constexpr (visit_stmt) {
            s << get_indent(indent_level);
            this->visitDefault(sym);
            s << ";" << std::endl;
        }
    }

    [[maybe_unused]] void handle(const slang::ForLoopStatement &loop) {
        s << get_indent(indent_level) << "for (";
        for (uint64_t i = 0; i < loop.initializers.size(); i++) {
            auto const *expr = loop.initializers[i];
            ExprCodeGenVisitor v(s);
            expr->visit(v);
            if (i != (loop.initializers.size() - 1)) {
                s << ", ";
            }
        }
        s << "; ";
        {
            ExprCodeGenVisitor v(s);
            loop.stopExpr->visit(v);
        }
        s << "; ";
        for (uint64_t i = 0; i < loop.steps.size(); i++) {
            auto const *expr = loop.steps[i];
            ExprCodeGenVisitor v(s);
            expr->visit(v);
            if (i != (loop.steps.size() - 1)) {
                s << ", ";
            }
        }
        s << ")";

        loop.body.visit(*this);
    }

    [[maybe_unused]] void handle(const slang::InstanceSymbol &inst) {
        auto const &def = inst.getDefinition();
        if (def.definitionKind == slang::DefinitionKind::Module) {
            if (!inst_) {
                inst_ = &inst;
                this->template visitDefault(inst);
            } else {
                // don't go deeper
                return;
            }
        }
    }

private:
    std::ostream &s;
    int &indent_level;
    const CXXCodeGenOptions &options;
    CodeGenModuleInformation &module_info;

    [[nodiscard]] std::string_view get_var_type(std::string_view name) const {
        if (options.use_4state) {
            return module_info.var_tracked(name) ? "xsim::runtime::logic_t" : "logic::logic";
        } else {
            return module_info.var_tracked(name) ? "xsim::runtime::bit_t" : "logic::bit";
        }
    }

    const slang::InstanceSymbol *inst_ = nullptr;
};

void codegen_sym(std::ostream &s, int &indent_level, const slang::Symbol *sym,
                 const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    CodeGenVisitor v(s, indent_level, options, info);
    sym->visit(v);
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

    s << get_indent(indent_level) << fmt::format("END_PROCESS({0});", ptr_name) << std::endl;
    indent_level--;
    s << get_indent(indent_level) << "};" << std::endl;
    s << get_indent(indent_level)
      << fmt::format("xsim::runtime::Scheduler::schedule_init({0});", ptr_name) << std::endl;

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
    if (process->kind == CombProcess::CombKind::GeneralPurpose) {
        // this is infinite loop
        throw std::runtime_error("General purpose always not not supposed yet");
    } else {
        // declare the always block

        s << get_indent(indent_level)
          << fmt::format("auto {0} = {1}->create_comb_process();", ptr_name, info.scheduler_name())
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

        // set input changed function
        s << get_indent(indent_level) << ptr_name << "->input_changed = [this]() {" << std::endl;
        indent_level++;
        s << get_indent(indent_level) << "bool res = false";
        for (auto *var : process->sensitive_list) {
            s << " || " << var->name << ".changed";
        }
        s << ";" << std::endl << get_indent(indent_level) << "return res;" << std::endl;
        indent_level--;
        s << get_indent(indent_level) << "};" << std::endl;

        // set the input changed cancel function
        s << get_indent(indent_level) << ptr_name << "->cancel_changed = [this]() {" << std::endl;
        indent_level++;
        for (auto *var : process->sensitive_list) {
            s << get_indent(indent_level) << var->name << ".changed = false;" << std::endl;
        }
        indent_level--;
        s << get_indent(indent_level) << "};" << std::endl;

        s << get_indent(indent_level) << fmt::format("comb_processes_.emplace_back({0});", ptr_name)
          << std::endl;
    }

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
    s << get_indent(indent_level) << ptr_name << "->running = true;" << std::endl;
    s << get_indent(indent_level) << ptr_name << "->finished = false;" << std::endl;

    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, indent_level, stmt, options, info);
    }

    s << get_indent(indent_level) << fmt::format("END_PROCESS({0});", ptr_name) << std::endl;

    indent_level--;
    s << get_indent(indent_level) << "};" << std::endl;

    s << get_indent(indent_level) << fmt::format("ff_process_.emplace_back({0});", ptr_name)
      << std::endl;

    // generate edge trigger functions
    s << get_indent(indent_level) << ptr_name << "->should_trigger = [this]() {" << std::endl;
    indent_level++;

    s << get_indent(indent_level) << "return false";
    for (auto const &[edge, v] : process->edges) {
        if (edge == slang::EdgeKind::PosEdge || edge == slang::EdgeKind::BothEdges) {
            s << fmt::format(" || {0}.should_trigger_posedge", v->name);
        }
        if (edge == slang::EdgeKind::NegEdge || edge == slang::EdgeKind::BothEdges) {
            s << fmt::format(" || {0}.should_trigger_negedge", v->name);
        }
    }
    indent_level--;
    s << ";" << std::endl << get_indent(indent_level) << "};" << std::endl;

    // compute trigger cancel
    s << get_indent(indent_level) << ptr_name << "->cancel_changed = [this]() {" << std::endl;
    indent_level++;

    std::set<std::string_view> vars;
    for (auto const &[edge, v] : process->edges) {
        if (edge == slang::EdgeKind::PosEdge || edge == slang::EdgeKind::BothEdges) {
            s << get_indent(indent_level)
              << fmt::format("{0}.should_trigger_posedge = false;", v->name) << std::endl;
        }
        if (edge == slang::EdgeKind::NegEdge || edge == slang::EdgeKind::BothEdges) {
            s << get_indent(indent_level)
              << fmt::format("{0}.should_trigger_negedge = false;", v->name) << std::endl;
        }
        vars.emplace(v->name);
    }

    indent_level--;
    s << get_indent(indent_level) << "};" << std::endl;

    // set edge tracking as well
    for (auto name : vars) {
        s << get_indent(indent_level) << name << ".track_edge = true;" << std::endl;
    }

    indent_level--;
    info.exit_process();
    s << get_indent(indent_level) << "}" << std::endl;
}

void output_header_file(const std::filesystem::path &filename, const Module *mod,
                        const CXXCodeGenOptions &options, CodeGenModuleInformation &info) {
    // analyze the dependencies to include which headers
    std::stringstream s;
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

    // need to look through the sensitivity list first
    for (auto const &comb : mod->comb_processes) {
        // label sensitivities variables
        for (auto const *sym : comb->sensitive_list) {
            info.add_tracked_name(sym->name);
        }
    }

    for (auto const &ff : mod->ff_processes) {
        for (auto const &[_, v] : ff->edges) {
            info.add_tracked_name(v->name);
        }
    }

    // all variables are public
    {
        CodeGenVisitor<false, false> v(s, indent_level, options, info);
        mod->def()->visit(v);
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

    if (!mod->comb_processes.empty()) {
        s << get_indent(indent_level) << "void comb(xsim::runtime::Scheduler *) override;"
          << std::endl;
    }

    if (!mod->ff_processes.empty()) {
        s << get_indent(indent_level) << "void ff(xsim::runtime::Scheduler *) override;"
          << std::endl;
    }

    indent_level--;

    s << get_indent(indent_level) << "};";

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

    int indent_level = 0;

    // initial block
    if (!mod->init_processes.empty()) {
        s << get_indent(indent_level) << "void " << mod->name << "::init(xsim::runtime::Scheduler *"
          << info.scheduler_name() << ") {" << std::endl;
        indent_level++;

        for (auto const &init : mod->init_processes) {
            codegen_init(s, indent_level, init.get(), options, info);
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

        indent_level--;
        s << get_indent(indent_level) << "}" << std::endl;
    }

    // always block
    if (!mod->comb_processes.empty()) {
        s << get_indent(indent_level) << "void " << mod->name << "::comb(xsim::runtime::Scheduler *"
          << info.scheduler_name() << ") {" << std::endl;
        indent_level++;

        for (auto const &comb : mod->comb_processes) {
            codegen_always(s, indent_level, comb.get(), options, info);
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

        indent_level--;
        s << get_indent(indent_level) << "}" << std::endl;
    }

    write_to_file(filename, s);
}

void output_main_file(const std::string &filename, const Module *top) {
    std::stringstream s;

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

    write_to_file(filename, s);
}

void CXXCodeGen::output(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto cc_filename = dir_path / get_cc_filename(top_->name);
    auto hh_filename = dir_path / get_hh_filename(top_->name);
    CodeGenModuleInformation info;
    output_header_file(hh_filename, top_, option_, info);
    output_cc_file(cc_filename, top_, option_, info);
    auto main_filename = dir_path / fmt::format("{0}.cc", main_name);
    output_main_file(main_filename, top_);
}

std::set<std::string_view> get_defs(const Module *module) {
    auto defs = module->get_defs();
    std::set<std::string_view> result;
    for (auto const *def : defs) {
        result.emplace(def->name);
    }
    return result;
}

void NinjaCodeGen::output(const std::string &dir) {
    std::filesystem::path dir_path = dir;
    auto ninja_filename = dir_path / "build.ninja";
    std::stringstream stream;
    // filling out missing information
    // use the output dir as the runtime dir
    if (options_.cxx_path.empty()) {
        // hope for the best?
        auto const *cxx = std::getenv("XSIM_CXX");
        if (cxx) {
            options_.cxx_path = cxx;
        } else {
            options_.cxx_path = "g++";
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
    stream << "  command = " << options_.cxx_path << " -MD -MF $out.d $cflags -c $in -o $out"
           << std::endl
           << std::endl;

    // output for each module definition
    auto defs = get_defs(top_);
    std::string objs;
    // add main object as well
    defs.emplace(main_name);
    for (auto const &name : defs) {
        auto obj_name = fmt::format("{0}.o", name);
        stream << "build " << obj_name << ": cc " << get_cc_filename(name) << std::endl;
        objs.append(obj_name).append(" ");
    }
    auto main_linkers = fmt::format("-pthread -lstdc++ -Wl,-rpath,{0}", lib_path.string());
    // build the main
    stream << "rule main" << std::endl;
    stream << "  command = " << options_.cxx_path << " $in " << runtime_lib_path << " $cflags "
           << main_linkers << " -o $out" << std::endl
           << std::endl;
    stream << "build " << options_.binary_name << ": main " << objs << std::endl;

    write_to_file(ninja_filename, stream);
}

}  // namespace xsim