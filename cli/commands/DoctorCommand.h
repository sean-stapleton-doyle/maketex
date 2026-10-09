#pragma once

#include "EffectiveOptions.h"

namespace maketex::cli {

// Checks that the TeX installation has what builds need — engines, tools,
// and, given a document, the packages and programs it uses. With
// --install-missing, installs what the document lacks. Returns 0 when
// everything required is present, kExitEnvironment otherwise.
int runDoctorCommand(const EffectiveOptions& options);

} // namespace maketex::cli
