#ifndef FSIM_EXPR_HH
#define FSIM_EXPR_HH

#include "slang/binding/SystemSubroutine.h"
#include "slang/compilation/Compilation.h"
#include "slang/symbols/ASTVisitor.h"
#include "util.hh"

namespace fsim {

auto constexpr FSIM_END_PROCESS = "END_PROCESS";

class ExprCodeGenVisitor : public slang::ASTVisitor<ExprCodeGenVisitor, false, true> {
public:
    ExprCodeGenVisitor(std::ostream &s, CodeGenModuleInformation &module_info)
        : s(s), module_info_(module_info) {}

    [[maybe_unused]] void handle(const slang::StringLiteral &str);

    [[maybe_unused]] void handle(const slang::IntegerLiteral &i);

    [[maybe_unused]] void handle(const slang::NamedValueExpression &n);

    [[maybe_unused]] void handle(const slang::ValueSymbol &sym);

    [[maybe_unused]] void handle(const slang::ConversionExpression &c);

    [[maybe_unused]] void handle(const slang::LValueReferenceExpression &);

    [[maybe_unused]] void handle(const slang::UnaryExpression &expr);

    [[maybe_unused]] void handle(const slang::BinaryExpression &expr);

    [[maybe_unused]] void handle(const slang::ConditionalExpression &expr);

    [[maybe_unused]] void handle(const slang::ElementSelectExpression &sym);

    [[maybe_unused]] void handle(const slang::RangeSelectExpression &expr);

    [[maybe_unused]] void handle(const slang::ConcatenationExpression &sym);

    [[maybe_unused]] void handle(const slang::CallExpression &expr);

    [[maybe_unused]] void handle(const slang::AssignmentExpression &expr);

private:
    std::ostream &s;
    CodeGenModuleInformation &module_info_;
    const slang::Expression *left_ptr = nullptr;
    void output_concat(const slang::ConcatenationExpression &concat);

    void output_timing(const slang::TimingControl &timing);

    bool is_return_symbol(const slang::Expression &expr);
};

class TimingControlCodeGen {
public:
    TimingControlCodeGen(std::ostream &s, int indent_level, CodeGenModuleInformation &module_info,
                         ExprCodeGenVisitor &expr_v);

    void handle(const slang::TimingControl &timing);

private:
    std::ostream &s;
    int indent_level;
    CodeGenModuleInformation &module_info_;
    ExprCodeGenVisitor &expr_v;
};

}  // namespace fsim

#endif  // FSIM_EXPR_HH
