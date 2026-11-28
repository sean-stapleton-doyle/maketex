#include "maketex/diagnostics/SourceResolver.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::diagnostics {

namespace {

    class FilesystemSourceResolver final : public ISourceResolver {
    public:
        bool isFile(const fs::path& p) const override
        {
            std::error_code ec;
            return fs::is_regular_file(p, ec);
        }

        fs::path canonical(const fs::path& p) const override
        {
            std::error_code ec;
            return fs::weakly_canonical(p, ec);
        }

        std::optional<std::string> readText(const fs::path& p) const override
        {
            std::ifstream in(p);
            if (!in)
                return std::nullopt;
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }
    };

} // namespace

const ISourceResolver& filesystemResolver()
{
    static const FilesystemSourceResolver kInstance;
    return kInstance;
}

} // namespace maketex::diagnostics
