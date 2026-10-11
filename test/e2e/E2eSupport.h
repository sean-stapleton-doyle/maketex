#pragma once

#include "../test_utils.h"
#include "CliRunner.h"
#include "FixtureHarness.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace maketex::e2e {

// Base fixture: a sandbox directory, a CliRunner isolated in it, and a skip
// when the engine the tests use isn't installed.
class E2eTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (!integration::hasProgram("pdflatex"))
            GTEST_SKIP() << "not installed: pdflatex";
        m_sandbox = test_utils::uniqueTempDir("maketex_e2e");
        std::filesystem::create_directories(m_sandbox);
        m_sandbox = std::filesystem::canonical(m_sandbox);
        m_cli = std::make_unique<CliRunner>(m_sandbox / "env");
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_sandbox, ec);
    }

    // A directory of the sandbox holding the given files.
    std::filesystem::path project(const std::string& name) const
    {
        auto dir = m_sandbox / name;
        std::filesystem::create_directories(dir);
        return dir;
    }

    static void write(const std::filesystem::path& file, const std::string& content)
    {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary | std::ios::trunc) << content;
    }

    static constexpr const char* kHello
        = "\\documentclass{article}\n\\begin{document}\nHello.\n\\end{document}\n";

    std::filesystem::path m_sandbox;
    std::unique_ptr<CliRunner> m_cli;
};

} // namespace maketex::e2e
