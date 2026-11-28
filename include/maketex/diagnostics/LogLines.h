#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace maketex::diagnostics {

// Splits log output on '\n' with the same boundary semantics as std::getline.
std::vector<std::string> splitLines(std::string_view output);

// Parses a base-10 integer from sv. Returns nullopt if sv is empty or has
// trailing characters.
std::optional<int> tryParseInt(std::string_view sv);

// Extracts the name between `…' in TeX/BibTeX messages such as
// "File `minted.sty' not found" or "I can't find file `chapter'".
std::string quotedName(std::string_view message);

} // namespace maketex::diagnostics
