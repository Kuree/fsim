#include "system_task.hh"

#include "module.hh"

namespace xsim::runtime {

std::string preprocess_display_fmt(const Module *module, std::string_view format) {
    // based on LRM 21.2
    std::string result;
    char pre[2] = {'\0', '\0'};
    uint64_t i = 0;
    while (i < format.size()) {
        auto c = format[i];
        // we only need to modify it make it consistent with the fmt lib string
        // also need to take into account of other weird things
        // for now we transform %m
        if (pre[0] == '%' && c == 'm') {
            // pop out the last %
            result.pop_back();
            // use the instance name
            result.append(module->hierarchy_name());
            c = '\0';
        }
        pre[0] = c;
        result.append(pre);
        i++;
    }

    return result;
}

}  // namespace xsim::runtime