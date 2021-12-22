#ifndef XSIM_STMT_HH
#define XSIM_STMT_HH

#include "cxx.hh"
#include "expr.hh"

namespace xsim {

class VarDeclarationVisitor : public slang::ASTVisitor<VarDeclarationVisitor, false, false> {
public:
    VarDeclarationVisitor(std::ostream &s, int &indent_level, const CXXCodeGenOptions &options,
                          CodeGenModuleInformation &module_info, ExprCodeGenVisitor &expr_v);

    [[maybe_unused]] void handle(const slang::VariableSymbol &var);

    [[maybe_unused]] void handle(const slang::NetSymbol &var);

    [[maybe_unused]] void handle(const slang::VariableDeclStatement &stmt);

    [[maybe_unused]] void handle(const slang::InstanceSymbol &inst);

private:
    std::ostream &s;
    int &indent_level;
    const CXXCodeGenOptions &options;
    CodeGenModuleInformation &module_info;
    ExprCodeGenVisitor &expr_v;

    const slang::InstanceSymbol *inst_ = nullptr;

    [[nodiscard]] std::string_view get_var_type(const slang::Type &t, std::string_view name) const;
};

class StmtCodeGenVisitor : public slang::ASTVisitor<StmtCodeGenVisitor, true, true> {
public:
    StmtCodeGenVisitor(std::ostream &s, int &indent_level, const CXXCodeGenOptions &options,
                       CodeGenModuleInformation &module_info);

    [[maybe_unused]] void handle(const slang::VariableSymbol &var);

    [[maybe_unused]] void handle(const slang::NetSymbol &var);

    [[maybe_unused]] void handle(const slang::VariableDeclStatement &stmt);

    [[maybe_unused]] void handle(const slang::TimedStatement &stmt);

    [[maybe_unused]] void handle(const slang::StatementBlockSymbol &);

    [[maybe_unused]] void handle(const slang::StatementList &list);

    [[maybe_unused]] void handle(const slang::ExpressionStatement &stmt);

    [[maybe_unused]] void handle(const slang::ConditionalStatement &stmt);

    [[maybe_unused]] void handle(const slang::ContinuousAssignSymbol &sym);

    [[maybe_unused]] void handle(const slang::ForLoopStatement &loop);

    [[maybe_unused]] void handle(const slang::RepeatLoopStatement &repeat);

    [[maybe_unused]] void handle(const slang::ForeverLoopStatement &forever);

    [[maybe_unused]] void handle(const slang::InstanceSymbol &inst);

private:
    std::ostream &s;
    int &indent_level;
    CodeGenModuleInformation &module_info;

    const slang::InstanceSymbol *inst_ = nullptr;
    ExprCodeGenVisitor expr_v;
    VarDeclarationVisitor decl_v;
};

}  // namespace xsim

#endif  // XSIM_STMT_HH
