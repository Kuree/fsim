#include "codegen.hh"

#include <filesystem>
#include <fstream>
#include <set>

#include "fmt/format.h"
#include "slang/binding/SystemSubroutine.h"

namespace xsim {

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

void output_header_file(const std::filesystem::path &filename, const Module *mod) {
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

    // init function
    if (!mod->init_processes.empty()) {
        s << get_indent(indent_level) << "void init(xsim::runtime::Scheduler *scheduler) override;"
          << std::endl;
    }

    indent_level--;

    s << get_indent(indent_level) << "};";
}

class ExprCodeGenVisitor : public slang::ASTVisitor<ExprCodeGenVisitor, false, true> {
public:
    explicit ExprCodeGenVisitor(std::ostream &s) : s(s) {}
    [[maybe_unused]] void handle(const slang::StringLiteral &str) {
        s << '\"' << str.getValue() << '\"';
    }
    std::ostream &s;
};

class CodeGenVisitor : public slang::ASTVisitor<CodeGenVisitor, true, true> {
    // the ultimate visitor
public:
    CodeGenVisitor(std::ostream &s, int &indent_level) : s(s), indent_level(indent_level) {}

    [[maybe_unused]] void handle(const slang::StatementList &list) {
        // entering a scope
        s << get_indent(indent_level) << "{";
        indent_level++;
        visitDefault(list);
        indent_level--;
        s << get_indent(indent_level) << "}" << std::endl;
    }

    [[maybe_unused]] void handle(const slang::ExpressionStatement &stmt) {
        s << std::endl << get_indent(indent_level);
        visitDefault(stmt);
        s << ";" << std::endl;
    }

    [[maybe_unused]] void handle(const slang::CallExpression &expr) {
        if (expr.subroutine.index() == 1) {
            auto const &info = std::get<1>(expr.subroutine);
            auto name = info.subroutine->name;
            // remove the leading $
            s << name.substr(1) << "(";
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
};

void codegen_sym(std::ostream &s, int &indent_level, const slang::Symbol *sym) {
    CodeGenVisitor v(s, indent_level);
    sym->visit(v);
}

void codegen_init(std::ostream &s, int &indent_level, const Process *process) {
    s << get_indent(indent_level) << "{" << std::endl;
    indent_level++;

    s << get_indent(indent_level) << "auto init_ptr = std::make_shared<InitialProcess>();"
      << std::endl
      << get_indent(indent_level) << "init_ptr->func = [this, init_ptr]() {" << std::endl;
    indent_level++;
    auto const &stmts = process->stmts;
    for (auto const *stmt : stmts) {
        codegen_sym(s, indent_level, stmt);
    }

    s << get_indent(indent_level) << "init_ptr->finished = true;" << std::endl
      << get_indent(indent_level) << "init_ptr->cond.signal();" << std::endl;
    indent_level--;
    s << get_indent(indent_level) << "};" << std::endl;
    s << get_indent(indent_level) << "scheduler->schedule_init(init_ptr);" << std::endl;

    indent_level--;
    s << get_indent(indent_level) << "}" << std::endl;
}

void output_cc_file(const std::filesystem::path &filename, const Module *mod) {
    std::ofstream s(filename, std::ios::trunc);
    auto hh_filename = get_hh_filename(mod->name);
    s << "#include \"" << hh_filename << "\"" << std::endl;
    // include more stuff
    s << "#include \"runtime/scheduler.hh\"" << std::endl;

    // we use "using namespace" here to make the code cleaner
    s << "using namespace xsim::runtime;" << std::endl;

    int indent_level = 0;

    if (!mod->init_processes.empty()) {
        s << get_indent(indent_level) << "void " << mod->name << "::init(Scheduler *scheduler) {"
          << std::endl;
        indent_level++;

        for (auto const &init : mod->init_processes) {
            codegen_init(s, indent_level, init.get());
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
    output_header_file(hh_filename, top_);
    output_cc_file(cc_filename, top_);
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
    if (options_.runtime_path.empty()) {
        auto p = std::filesystem::path(dir);
        options_.runtime_path = p.parent_path();
    }
    if (options_.clang_path.empty()) {
        // hope for the best?
        options_.clang_path = "clang";
    }
    if (options_.binary_name.empty()) {
        options_.binary_name = default_output_name;
    }

    std::filesystem::path runtime_dir = options_.runtime_path;
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