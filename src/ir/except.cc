#include "except.hh"

#include "slang/diagnostics/TextDiagnosticClient.h"

namespace xsim {

inline constexpr slang::DiagCode InvalidSyntax(slang::DiagSubsystem::Compilation, 100);

void report_code(const std::shared_ptr<slang::TextDiagnosticClient> &client,
                 const std::string &what, slang::DiagCode code, slang::SourceLocation loc) {
    slang::Diagnostic diag(code, loc);
    diag << what;
    slang::ReportedDiagnostic reported_diag(diag);
    reported_diag.severity = slang::DiagnosticSeverity::Fatal;
    reported_diag.location = loc;
    reported_diag.formattedMessage = what;
    client->report(reported_diag);
}

void NotSupportedException::report(
    const std::shared_ptr<slang::TextDiagnosticClient> &client) const {
    report_code(client, std::runtime_error::what(), slang::diag::NotYetSupported, loc_);
}

void InvalidSyntaxException::report(
    const std::shared_ptr<slang::TextDiagnosticClient> &client) const {
    report_code(client, std::runtime_error::what(), InvalidSyntax, loc_);
}

}  // namespace xsim