#ifndef XSIM_EXCEPT_HH
#define XSIM_EXCEPT_HH

#include <slang/text/SourceLocation.h>

#include <exception>

namespace slang {
class TextDiagnosticClient;
}

namespace xsim {

class Exception : public std::runtime_error {
public:
    explicit Exception(const std::string &what) : std::runtime_error(what) {}
    virtual void report(const std::shared_ptr<slang::TextDiagnosticClient> &client) const = 0;

    ~Exception() override = default;
};

class NotSupportedException : public Exception {
public:
    NotSupportedException(const std::string &what, slang::SourceLocation loc)
        : Exception(what), loc_(loc) {}

    void report(const std::shared_ptr<slang::TextDiagnosticClient> &client) const override;

private:
    slang::SourceLocation loc_;
};

class InvalidSyntaxException: public Exception {
public:
    InvalidSyntaxException(const std::string &what, slang::SourceLocation loc)
        : Exception(what), loc_(loc) {}

    void report(const std::shared_ptr<slang::TextDiagnosticClient> &client) const override;

private:
    slang::SourceLocation loc_;
};

}  // namespace xsim

#endif  // XSIM_EXCEPT_HH
