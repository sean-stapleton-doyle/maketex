#pragma once

namespace maketex {

// Return type of ToolBase::decide() and ToolSpec's internal trigger evaluation.
//
//   Run  — the tool should execute this pass.
//   Skip — nothing to do: trigger file unchanged, not applicable, or no inputs found.
//   Done — tool already ran this build session and will not run again, even if its
//          trigger file changes. Used for tools that must run at most once per build
//          (biber, bibtex) to prevent spurious reruns caused by latex rewriting their
//          input files after the first run.
enum class RunDecision { Run,
    Skip,
    Done };

} // namespace maketex
