#include "maketex/parsers/BcfParser.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using maketex::BcfParser;
namespace fs = std::filesystem;

namespace {

class BcfParserTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_bcf_test"; }
};

} // namespace

TEST_F(BcfParserTest, extractsFileDataSources)
{
    auto bcf = writeFile("main.bcf",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<bcf:controlfile version=\"3.11\">\n"
        "  <bcf:bibdata section=\"0\">\n"
        "    <bcf:datasource type=\"file\" datatype=\"bibtex\" glob=\"false\">refs.bib</bcf:datasource>\n"
        "    <bcf:datasource type=\"file\" datatype=\"bibtex\" glob=\"false\">more/extra.bib</bcf:datasource>\n"
        "    <bcf:datasource type=\"url\" datatype=\"bibtex\">https://example.org/x.bib</bcf:datasource>\n"
        "  </bcf:bibdata>\n"
        "</bcf:controlfile>\n");

    auto rec = BcfParser { }.parse(bcf);
    ASSERT_TRUE(rec.has_value());
    ASSERT_EQ(rec->dataSources.size(), 2u);
    EXPECT_EQ(rec->dataSources[0], "refs.bib");
    EXPECT_EQ(rec->dataSources[1], "more/extra.bib");
}

TEST_F(BcfParserTest, missingFileIsAnError)
{
    EXPECT_FALSE(BcfParser { }.parse(m_dir / "absent.bcf").has_value());
}
