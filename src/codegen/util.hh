#ifndef FSIM_CODEGEN_UTIL_HH
#define FSIM_CODEGEN_UTIL_HH
#include <sstream>
#include <stack>

#include "../ir/ir.hh"
#include "fmt/format.h"

namespace fsim {
// module is an illegal name in SV. using it will guarantee that
// there is no conflicts
auto constexpr main_name = "module";
auto constexpr default_output_name = "fsim.out";

void write_to_file(const std::string &filename, std::stringstream &stream, bool format = true);

namespace util::fs {
std::optional<std::string> which(std::string_view name);
std::string join(std::string_view path1, std::string_view path2);
}  // namespace util::fs

namespace util::string {
std::vector<std::string> get_tokens(const std::string &line, const std::string &delimiter);
}

template <typename T>
inline std::string get_cc_filename(const T &name) {
    return fmt::format("{0}.cc", name);
}

template <typename T>
inline std::string get_hh_filename(const T &name) {
    return fmt::format("{0}.hh", name);
}

class CodeGenModuleInformation {
public:
    // used to hold different information while passing between different visitors

    std::string get_new_name(const std::string &prefix, bool track_new_name = true);

    [[maybe_unused]] inline void add_used_names(std::string_view name) {
        used_names_.emplace(std::string(name));
    }

    std::string enter_process();
    [[nodiscard]] bool has_process() const { return !process_names_.empty(); }

    std::string current_process_name() const { return process_names_.top(); }
    const std::string &scheduler_name();

    [[nodiscard]] inline bool var_tracked(std::string_view name) const {
        return tracked_vars_.find(name) != tracked_vars_.end();
    }

    void inline clear_tracked_names() { tracked_vars_.clear(); }
    void inline add_tracked_name(std::string_view name) { tracked_vars_.emplace(name); }

    void exit_process();

    const Module *current_module = nullptr;
    const slang::SubroutineSymbol *current_function = nullptr;

    const slang::Compilation *get_compilation() const;

    std::string_view get_identifier_name(std::string_view name);

private:
    std::stack<std::string> process_names_;
    std::unordered_set<std::string> used_names_;
    std::unordered_set<std::string_view> tracked_vars_;

    // SystemVerilog allows escape for weird names
    // e.g. escaped identifiers
    std::unordered_map<std::string_view, std::string> renamed_identifier_;

    std::string scheduler_name_;
};

std::pair<std::string_view, uint32_t> get_loc(const slang::SourceLocation &loc,
                                              const slang::Compilation *compilation);

}  // namespace fsim
#endif  // FSIM_CODEGEN_UTIL_HH
