#include "maketex/build/JobName.h"

#include <string_view>
#include <vector>

namespace maketex::build {

std::pair<EngineArguments, std::optional<std::string>> splitJobName(
    EngineArguments arguments)
{
    std::optional<std::string> jobName;
    std::vector<std::string> kept;
    auto& opts = arguments.extraOptions;
    for (std::size_t i = 0; i < opts.size(); ++i) {
        std::string_view opt = opts[i];
        if (opt.starts_with("--"))
            opt.remove_prefix(1);
        if (opt.starts_with("-jobname=")) {
            jobName = std::string(opt.substr(9));
        } else if (opt == "-jobname" && i + 1 < opts.size()) {
            jobName = opts[++i];
        } else {
            kept.push_back(opts[i]);
        }
    }
    opts = std::move(kept);
    if (jobName && jobName->empty())
        jobName.reset();
    return { std::move(arguments), std::move(jobName) };
}

} // namespace maketex::build
