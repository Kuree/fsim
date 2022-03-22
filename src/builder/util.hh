#ifndef FSIM_BUILDER_UTIL_HH
#define FSIM_BUILDER_UTIL_HH

#include <string>
#include <vector>

namespace fsim::string {
std::vector<std::string> get_tokens(const std::string &line, const std::string &delimiter);
}

namespace fsim::platform {
class DLOpenHelper {
public:
    explicit DLOpenHelper(const std::string &filename);
    DLOpenHelper(const std::string &filename, int mode);

    ~DLOpenHelper();

    [[nodiscard]] void *get_sym(const std::string &name) const;

    void *ptr = nullptr;

private:
    void load(const char *name, int mode);
};
}  // namespace fsim::platform

#endif  // FSIM_BUILDER_UTIL_HH
