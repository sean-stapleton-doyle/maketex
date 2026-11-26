#pragma once

#include "maketex/InvokerKind.h"
#include "maketex/ToolContext.h"
#include "maketex/ToolInvocation.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace maketex {

// One thing an auxiliary tool's result depends on.
struct ToolInput {
    // Absolute path of the file. For a derived input (content set) this is
    // a virtual key such as "<out>/main.aux#bibtex": it names the part of a
    // file the content was taken from and is never opened.
    std::filesystem::path path;
    // Set for a derived input: the bytes whose digest stands in for the
    // file's. Unset means "hash the file at path".
    std::optional<std::string> content;
};

// Which directory an auxiliary tool prefers as its working directory.
enum class ToolWorkingDir {
    Output, // outputDir (default; required by bibtex openout_any=p)
    Source, // the document's directory (so relative ../ paths resolve)
};

// Auxiliary LaTeX tools (biber, bibtex, makeindex, etc.).
//
// Tools are pure descriptions: whether the document uses them, what their
// result depends on, and how to invoke them. BuildDriver owns scheduling —
// it runs a tool when it applies and its inputs differ from the ones its
// last successful run consumed (recorded in the state database), so the
// same rule decides within a build and across invocations.
class ITool {
public:
    virtual ~ITool() = default;

    // Human-readable name, e.g. "bibtex".
    virtual std::string_view name() const = 0;

    // Kind tag stamped onto the ToolInvocation before launch. Used by
    // DiagnosticExtractor to dispatch to the right parser.
    virtual InvokerKind invokerKind() const = 0;

    // Returns the full argv. argv[0] is looked up via PATH.
    virtual std::vector<std::string> buildArgs(const ToolInvocation& inv) const = 0;

    // Where BuildDriver launches the tool. Defaults to Output (outputDir).
    virtual ToolWorkingDir workingDirPreference() const { return ToolWorkingDir::Output; }

    // Whether the document uses this tool, judged from the latest engine
    // pass — e.g. biber applies when that pass wrote the .bcf. Deciding from
    // the pass rather than from files on disk keeps a stale .bcf left over
    // from an earlier version of the document from firing biber forever.
    virtual bool applies(const ToolContext& ctx) const = 0;

    // Everything the tool's result depends on: trigger files, databases,
    // styles, derived digests. Inputs that do not exist are omitted.
    virtual std::vector<ToolInput> inputs(const ToolContext& ctx) const = 0;

    // Files the tool writes that the engine reads back (.bbl, .ind). After a
    // run, the engine reruns only if one of them changed. Empty means
    // unknown: every run is assumed to change the document.
    virtual std::vector<std::filesystem::path> outputs(const ToolContext& ctx) const = 0;
};

} // namespace maketex
