#include "Config.h"

#include <toml++/toml.hpp>

#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace maketex::cli {

std::optional<fs::path> findConfigFile(const fs::path& startDir)
{
    std::error_code ec;
    fs::path dir = fs::absolute(startDir, ec);
    if (ec)
        return std::nullopt;

    // The search is scoped to the project: it stops at the repository root
    // (a directory holding .git, .hg, or .svn) and never reaches the home
    // directory, so a stray ~/maketex.toml with `main` set cannot hijack
    // builds of unrelated documents.
    fs::path home;
    if (const char* h = std::getenv("HOME"); h && *h)
        home = fs::weakly_canonical(h, ec);
    while (true) {
        if (!home.empty() && fs::weakly_canonical(dir, ec) == home)
            return std::nullopt;
        fs::path candidate = dir / "maketex.toml";
        if (fs::is_regular_file(candidate, ec))
            return candidate;
        for (const char* vcs : { ".git", ".hg", ".svn" })
            if (fs::exists(dir / vcs, ec))
                return std::nullopt;
        fs::path parent = dir.parent_path();
        if (parent == dir)
            return std::nullopt;
        dir = std::move(parent);
    }
}

namespace {

    // Accumulates user-facing "file:line: message" strings.
    class Errors {
    public:
        explicit Errors(const fs::path& sourceFile)
            : m_sourceFile(sourceFile)
        {
        }

        void add(const toml::node& node, std::string_view message)
        {
            m_messages.push_back(std::format("{}:{}: {}",
                m_sourceFile.string(), node.source().begin.line, message));
        }

        bool empty() const { return m_messages.empty(); }

        std::string join() const
        {
            std::string joined;
            for (const auto& m : m_messages) {
                if (!joined.empty())
                    joined += "\n";
                joined += m;
            }
            return joined;
        }

    private:
        const fs::path& m_sourceFile;
        std::vector<std::string> m_messages;
    };

    template <typename T>
    std::optional<T> getScalar(const toml::table& table, std::string_view key,
        std::string_view typeName, Errors& errors)
    {
        const toml::node* node = table.get(key);
        if (!node)
            return std::nullopt;
        if (auto value = node->value<T>())
            return value;
        errors.add(*node, std::format("`{}` must be a {}", key, typeName));
        return std::nullopt;
    }

    std::optional<std::vector<std::string>> getStringArray(
        const toml::table& table, std::string_view key, Errors& errors)
    {
        const toml::node* node = table.get(key);
        if (!node)
            return std::nullopt;
        const toml::array* array = node->as_array();
        if (!array) {
            errors.add(*node, std::format("`{}` must be an array of strings", key));
            return std::nullopt;
        }
        std::vector<std::string> values;
        for (const toml::node& element : *array) {
            if (auto s = element.value<std::string>()) {
                values.push_back(std::move(*s));
            } else {
                errors.add(element,
                    std::format("`{}` must contain only strings", key));
                return std::nullopt;
            }
        }
        return values;
    }

    void warnUnknownKeys(const toml::table& table, std::string_view tableName,
        std::initializer_list<std::string_view> known,
        const fs::path& sourceFile, std::vector<std::string>& warnings)
    {
        for (const auto& [key, node] : table) {
            if (std::find(known.begin(), known.end(), key.str()) != known.end())
                continue;
            std::string where = tableName.empty()
                ? std::string(key.str())
                : std::format("{}.{}", tableName, key.str());
            warnings.push_back(std::format("{}:{}: unknown key `{}`",
                sourceFile.string(), node.source().begin.line, where));
        }
    }

    fs::path resolveAgainst(const fs::path& base, const fs::path& p)
    {
        return p.is_absolute() ? p : base / p;
    }

} // namespace

std::expected<Config, std::string> parseConfig(std::string_view toml,
    const fs::path& sourceFile)
{
    toml::table table;
    try {
        table = toml::parse(toml, sourceFile.string());
    } catch (const toml::parse_error& e) {
        return std::unexpected(std::format("{}:{}: {}",
            sourceFile.string(), e.source().begin.line,
            std::string_view(e.description())));
    }

    Config config;
    config.sourceFile = sourceFile;
    const fs::path baseDir = sourceFile.parent_path();
    Errors errors(sourceFile);

    warnUnknownKeys(table, "",
        { "main", "engine", "output-dir", "tools", "shell-escape", "synctex",
            "halt-on-error", "max-passes", "timeout", "engine-args", "watch", "clean" },
        sourceFile, config.warnings);

    if (auto main = getScalar<std::string>(table, "main", "string", errors))
        config.main = resolveAgainst(baseDir, *main);

    if (auto engine = getScalar<std::string>(table, "engine", "string", errors)) {
        config.engine = parseEngineKind(*engine);
        if (!config.engine)
            errors.add(*table.get("engine"),
                std::format("unknown engine `{}` (expected pdflatex, xelatex, "
                            "or lualatex)",
                    *engine));
    }

    if (auto dir = getScalar<std::string>(table, "output-dir", "string", errors))
        config.outputDir = resolveAgainst(baseDir, *dir);

    if (auto tools = getStringArray(table, "tools", errors)) {
        std::vector<ToolKind> kinds;
        for (const auto& name : *tools) {
            if (auto kind = parseToolKind(name))
                kinds.push_back(*kind);
            else
                errors.add(*table.get("tools"),
                    std::format("unknown tool `{}`", name));
        }
        config.tools = std::move(kinds);
    }

    config.shellEscape = getScalar<bool>(table, "shell-escape", "boolean", errors);
    config.synctex = getScalar<bool>(table, "synctex", "boolean", errors);
    config.haltOnError = getScalar<bool>(table, "halt-on-error", "boolean", errors);

    // Positive and small enough for an int: a huge TOML integer must not
    // wrap into a tiny one.
    auto positiveInt = [&](std::string_view key) -> std::optional<int> {
        auto value = getScalar<int64_t>(table, key, "integer", errors);
        if (!value)
            return std::nullopt;
        if (*value <= 0 || *value > std::numeric_limits<int>::max()) {
            errors.add(*table.get(key), std::format("`{}` must be a positive integer", key));
            return std::nullopt;
        }
        return static_cast<int>(*value);
    };
    config.maxPasses = positiveInt("max-passes");
    config.timeoutSeconds = positiveInt("timeout");

    if (const toml::node* node = table.get("engine-args")) {
        if (const toml::table* engineArgs = node->as_table()) {
            warnUnknownKeys(*engineArgs, "engine-args",
                { "program", "extra-options" }, sourceFile, config.warnings);
            config.engineProgram = getScalar<std::string>(*engineArgs, "program",
                "string", errors);
            if (auto opts = getStringArray(*engineArgs, "extra-options", errors))
                config.engineOptions = std::move(*opts);
        } else {
            errors.add(*node, "`engine-args` must be a table");
        }
    }

    if (const toml::node* node = table.get("watch")) {
        if (const toml::table* watch = node->as_table()) {
            warnUnknownKeys(*watch, "watch", { "interval-ms", "extra-paths" },
                sourceFile, config.warnings);
            if (auto ms = getScalar<int64_t>(*watch, "interval-ms", "integer",
                    errors)) {
                if (*ms <= 0)
                    errors.add(*watch->get("interval-ms"),
                        "`watch.interval-ms` must be positive");
                else
                    config.watchIntervalMs = static_cast<int>(*ms);
            }
            if (auto paths = getStringArray(*watch, "extra-paths", errors)) {
                for (const auto& p : *paths)
                    config.watchExtraPaths.push_back(resolveAgainst(baseDir, p));
            }
        } else {
            errors.add(*node, "`watch` must be a table");
        }
    }

    if (const toml::node* node = table.get("clean")) {
        if (const toml::table* clean = node->as_table()) {
            warnUnknownKeys(*clean, "clean", { "extra-extensions" }, sourceFile,
                config.warnings);
            if (auto exts = getStringArray(*clean, "extra-extensions", errors)) {
                for (auto& ext : *exts) {
                    if (!ext.starts_with('.'))
                        ext.insert(ext.begin(), '.');
                    config.cleanExtraExtensions.push_back(std::move(ext));
                }
            }
        } else {
            errors.add(*node, "`clean` must be a table");
        }
    }

    if (!errors.empty())
        return std::unexpected(errors.join());
    return config;
}

std::expected<Config, std::string> loadConfig(const fs::path& path)
{
    std::ifstream file(path);
    if (!file)
        return std::unexpected(
            std::format("cannot read config file: {}", path.string()));
    std::ostringstream contents;
    contents << file.rdbuf();
    return parseConfig(contents.view(), path);
}

} // namespace maketex::cli
