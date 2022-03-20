#ifndef FSIM_EXCEPT_HH
#define FSIM_EXCEPT_HH

#include <slang/text/SourceLocation.h>

#include <exception>

namespace slang {
class TextDiagnosticClient;
}

namespace fsim {

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

class InvalidSyntaxException : public Exception {
public:
    InvalidSyntaxException(const std::string &what, slang::SourceLocation loc)
        : Exception(what), loc_(loc) {}

    void report(const std::shared_ptr<slang::TextDiagnosticClient> &client) const override;

private:
    slang::SourceLocation loc_;
};

class InvalidInput : public Exception {
public:
    explicit InvalidInput(const std::string &what) : Exception(what) {}
    void report(const std::shared_ptr<slang::TextDiagnosticClient> &) const override;
};

class InternalError : public Exception {
public:
    explicit InternalError(const std::string &what) : Exception(what) {}
    void report(const std::shared_ptr<slang::TextDiagnosticClient> &) const override;
};

}  // namespace fsim

#endif  // FSIM_EXCEPT_HH
