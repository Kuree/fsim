#ifndef FSIM_STMT_HH
#define FSIM_STMT_HH

#include "cxx.hh"
#include "expr.hh"

namespace fsim {

class VarDeclarationVisitor : public slang::ASTVisitor<VarDeclarationVisitor, false, false> {
public:
    VarDeclarationVisitor(std::ostream &s, const CXXCodeGenOptions &options,
                          CodeGenModuleInformation &module_info, ExprCodeGenVisitor &expr_v);

    [[maybe_unused]] void handle(const slang::VariableSymbol &var);

    [[maybe_unused]] void handle(const slang::NetSymbol &var);

    [[maybe_unused]] void handle(const slang::VariableDeclStatement &stmt);

    [[maybe_unused]] void handle(const slang::InstanceSymbol &inst);

    [[maybe_unused]] void handle(const slang::ParameterSymbol &param);

private:
    std::ostream &s;
    const CXXCodeGenOptions &options;
    CodeGenModuleInformation &module_info;
    ExprCodeGenVisitor &expr_v;

    const slang::InstanceSymbol *inst_ = nullptr;

    [[nodiscard]] std::string get_var_decl(const slang::Symbol &sym) const;

    void handle_(const slang::ValueSymbol &var,
                 slang::VariableLifetime = slang::VariableLifetime::Automatic);
};

class StmtCodeGenVisitor : public slang::ASTVisitor<StmtCodeGenVisitor, true, true> {
public:
    StmtCodeGenVisitor(std::ostream &s, const CXXCodeGenOptions &options,
                       CodeGenModuleInformation &module_info);

    [[maybe_unused]] void handle(const slang::VariableSymbol &var);

    [[maybe_unused]] void handle(const slang::NetSymbol &var);

    [[maybe_unused]] void handle(const slang::VariableDeclStatement &stmt);

    [[maybe_unused]] void handle(const slang::TimedStatement &stmt);

    [[maybe_unused]] void handle(const slang::StatementBlockSymbol &);

    [[maybe_unused]] void handle(const slang::BlockStatement &stmt);

    [[maybe_unused]] void handle(const slang::StatementList &list);

    [[maybe_unused]] void handle(const slang::ExpressionStatement &stmt);

    [[maybe_unused]] void handle(const slang::ConditionalStatement &stmt);

    [[maybe_unused]] void handle(const slang::ContinuousAssignSymbol &sym);

    [[maybe_unused]] void handle(const slang::ForLoopStatement &loop);

    [[maybe_unused]] void handle(const slang::CaseStatement &stmt);

    [[maybe_unused]] void handle(const slang::RepeatLoopStatement &repeat);

    [[maybe_unused]] void handle(const slang::ForeverLoopStatement &forever);

    [[maybe_unused]] void handle(const slang::InstanceSymbol &inst);

    [[maybe_unused]] void handle(const slang::ReturnStatement &ret);

private:
    std::ostream &s;
    CodeGenModuleInformation &module_info;

    const slang::InstanceSymbol *inst_ = nullptr;
    ExprCodeGenVisitor expr_v;
    VarDeclarationVisitor decl_v;
};

// helper functions
std::string get_symbol_type(const slang::Symbol &sym, CodeGenModuleInformation &module_info,
                            const CXXCodeGenOptions &options, std::string_view name_prefix = "");

}  // namespace fsim

#endif  // FSIM_STMT_HH
