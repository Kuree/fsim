#ifndef XSIM_EXPR_HH
#define XSIM_EXPR_HH

#include "slang/binding/SystemSubroutine.h"
#include "slang/compilation/Compilation.h"
#include "slang/symbols/ASTVisitor.h"
#include "util.hh"

namespace xsim {
class ExprCodeGenVisitor : public slang::ASTVisitor<ExprCodeGenVisitor, false, true> {
public:
    ExprCodeGenVisitor(std::ostream &s, CodeGenModuleInformation &module_info)
        : s(s), module_info_(module_info) {}

    [[maybe_unused]] void handle(const slang::StringLiteral &str);

    [[maybe_unused]] void handle(const slang::IntegerLiteral &i);

    [[maybe_unused]] void handle(const slang::NamedValueExpression &n);

    void handle(const slang::ValueSymbol &sym);

    [[maybe_unused]] void handle(const slang::ConversionExpression &c);

    [[maybe_unused]] void handle(const slang::LValueReferenceExpression &);

    [[maybe_unused]] void handle(const slang::UnaryExpression &expr);

    [[maybe_unused]] void handle(const slang::BinaryExpression &expr);

    [[maybe_unused]] void handle(const slang::ElementSelectExpression &sym);

    [[maybe_unused]] void handle(const slang::ConcatenationExpression &sym);

    [[maybe_unused]] void handle(const slang::CallExpression &expr);

    [[maybe_unused]] void handle(const slang::AssignmentExpression &expr);

private:
    std::ostream &s;
    CodeGenModuleInformation &module_info_;
    const slang::Expression *left_ptr = nullptr;
    void output_concat(const slang::ConcatenationExpression &concat);
};
}  // namespace xsim

#endif  // XSIM_EXPR_HH
