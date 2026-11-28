#include "maketex/driver/ToolWaves.h"

#include <algorithm>

namespace maketex::driver {

bool toolsConflict(const ToolFootprint& a, const ToolFootprint& b)
{
    if (a.outputsUnknown || b.outputsUnknown)
        return true;
    for (const auto& out : a.outputKeys)
        if (b.outputKeys.contains(out) || b.inputKeys.contains(out))
            return true;
    for (const auto& out : b.outputKeys)
        if (a.inputKeys.contains(out))
            return true;
    return false;
}

std::vector<std::vector<std::size_t>> planToolWaves(std::span<const ToolFootprint> tools)
{
    std::vector<std::vector<std::size_t>> waves;
    for (std::size_t idx = 0; idx < tools.size(); ++idx) {
        if (!waves.empty()) {
            bool canJoin = std::ranges::none_of(waves.back(), [&](std::size_t prev) {
                return toolsConflict(tools[prev], tools[idx]);
            });
            if (canJoin) {
                waves.back().push_back(idx);
                continue;
            }
        }
        waves.push_back({ idx });
    }
    return waves;
}

} // namespace maketex::driver
