#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace maketex::driver {

// Declared input/output footprint of one pending auxiliary tool, using
// weakly-canonical path keys.
struct ToolFootprint {
    std::unordered_set<std::string> inputKeys;
    std::unordered_set<std::string> outputKeys;
    // True when ITool::outputs() returned empty: the tool's outputs are not
    // declared, so it cannot safely share a wave with any other tool.
    bool outputsUnknown = false;
};

// Two tools conflict if either has unknown outputs, if they share an output
// path, or if one's declared output is the other's declared input.
bool toolsConflict(const ToolFootprint& a, const ToolFootprint& b);

// Greedily groups tools into waves that may safely execute in parallel while
// preserving roster order across waves.
std::vector<std::vector<std::size_t>> planToolWaves(std::span<const ToolFootprint> tools);

} // namespace maketex::driver
