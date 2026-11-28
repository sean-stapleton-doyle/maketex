#include "maketex/build/Fingerprint.h"

namespace maketex::build {

namespace {

    void appendCustomTools(std::string& fp,
        const std::vector<HashTriggeredTool::Recipe>& customTools)
    {
        for (const auto& c : customTools) {
            fp += ";custom=";
            fp += c.name;
            fp += ':';
            for (const auto& a : c.argv) {
                fp += a;
                fp += ' ';
            }
            for (const auto& t : c.triggerExtensions) {
                fp += t;
                fp += ',';
            }
            for (const auto& o : c.outputExtensions) {
                fp += o;
                fp += ',';
            }
            fp += c.workingDir == ToolWorkingDir::Source ? 'S' : 'O';
        }
    }

    void appendPolicyAndFilters(std::string& fp, const BuildOptions& options)
    {
        fp += ";shellEscape=";
        fp += options.shellEscape ? '1' : '0';
        fp += ";synctex=";
        fp += options.synctex ? '1' : '0';
        fp += ";haltOnError=";
        fp += options.haltOnError ? '1' : '0';
        fp += ";preambleCache=";
        fp += options.preambleCache ? '1' : '0';
        fp += ";werror=";
        fp += options.diagnosticPolicy.warningsAsErrors ? '1' : '0';
        for (const auto& f : options.diagnosticPolicy.failOn) {
            fp += ";failOn=";
            fp += f;
        }
        for (const auto& ig : options.diagnosticPolicy.ignore) {
            fp += ";ignore=";
            fp += ig;
        }
        for (const auto& [ext, pat] : options.contentFilters) {
            fp += ";filter=";
            fp += ext;
            fp += ':';
            fp += pat;
        }
    }

    void appendEnvAndPaths(std::string& fp, const BuildOptions& options,
        const EnvLookup& lookup)
    {
        for (const auto& [k, v] : options.extraEnv) {
            fp += ";env:";
            fp += k;
            fp += '=';
            fp += v;
        }
        auto appendPaths = [&](std::string_view tag, const std::vector<std::string>& ps) {
            if (ps.empty())
                return;
            fp += ';';
            fp += tag;
            fp += '=';
            for (const auto& p : ps) {
                fp += p;
                fp += ':';
            }
        };
        appendPaths("cfgTEXINPUTS", options.searchPaths.texInputs);
        appendPaths("cfgBIBINPUTS", options.searchPaths.bibInputs);
        appendPaths("cfgBSTINPUTS", options.searchPaths.bstInputs);
        appendPaths("cfgINDEXSTYLE", options.searchPaths.indexStyle);
        // Where the engine and tools look for inputs changes what they
        // produce as surely as a flag does. The user's own values only: the
        // document directory maketex adds must stay out, or moving the
        // project (a restored CI cache) would reset its state.
        for (std::string_view var : { "TEXINPUTS", "BIBINPUTS", "BSTINPUTS", "INDEXSTYLE" }) {
            fp += ';';
            fp += var;
            fp += '=';
            if (auto value = lookup(var))
                fp += *value;
        }
    }

} // namespace

std::string buildFingerprint(const BuildOptions& options,
    std::string_view engineName,
    std::span<const std::string_view> toolNames,
    const EnvLookup& env)
{
    const EnvLookup lookup = env ? env : processEnv();
    std::string fp = "engine=";
    fp += engineName;
    fp += ";program=";
    fp += options.engineArguments.program;
    fp += ";opts=";
    for (const auto& opt : options.engineArguments.extraOptions) {
        fp += opt;
        fp += ',';
    }
    fp += ";tools=";
    for (auto name : toolNames) {
        fp += name;
        fp += ',';
    }
    appendCustomTools(fp, options.customTools);
    appendPolicyAndFilters(fp, options);
    appendEnvAndPaths(fp, options, lookup);
    return fp;
}

} // namespace maketex::build
