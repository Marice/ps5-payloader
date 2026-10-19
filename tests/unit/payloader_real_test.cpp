// PayLoader - Tests on replies captured from pldmgr 0.6.1 on a PS5 Pro (13.60), 2026-10-19.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/pldmgr.hpp"
#include "payloader/repo_status.hpp"
#include "payloader/versions.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace hui::payloader
{
namespace
{

std::string captured(const char *name)
{
    std::ifstream file(std::string(HUI_SOURCE_DIR) + "/tests/unit/fixtures/pldmgr-0.6.1/" + name +
                       ".txt");
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

TEST(PayloaderCaptured, ReadsTheVersion)
{
    EXPECT_TRUE(version_at_least(captured("version"), 0, 6, 0));
}

TEST(PayloaderCaptured, ReadsInstalledPayloads)
{
    PayloadList list;
    ASSERT_TRUE(parse_payloads(captured("list_payloads"), list));
    EXPECT_EQ(list.running_version, "0.6.1");
    ASSERT_EQ(list.items.size(), 11u);
    EXPECT_EQ(list.items[0].slot, "repo/websrv@e8cce5d1");
    EXPECT_EQ(list.items[0].version, "v0.34");
    EXPECT_EQ(list.items[3].kind, "local");
    EXPECT_EQ(list.items[3].title(), "etaHEN");
}

TEST(PayloaderCaptured, ReadsTheConfig)
{
    Config config;
    ASSERT_TRUE(parse_config(captured("get_config"), config));
    EXPECT_FALSE(config.autoload_enabled);
    EXPECT_EQ(config.autoload_delay, 5);
    EXPECT_TRUE(config.auto_browser_open);
    EXPECT_TRUE(config.auto_install_app);
    EXPECT_NE(config.autoload_list.find("local/ps5-exporter"), std::string::npos);
}

TEST(PayloaderCaptured, ReadsAutoloadStatusProcessesAndSources)
{
    AutoloadStatus status;
    ASSERT_TRUE(parse_autoload_status(captured("autoload_status"), status));
    EXPECT_EQ(status.state, AutoloadState::disabled);

    std::vector<Process> processes;
    ASSERT_TRUE(parse_processes(captured("processes_list"), processes));
    int payloads = 0;
    for (const Process &p : processes)
        payloads += p.payload ? 1 : 0;
    EXPECT_EQ(processes.size(), 91u);
    EXPECT_EQ(payloads, 9);

    std::vector<Source> sources;
    ASSERT_TRUE(parse_sources(captured("sources_list"), sources));
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_FALSE(sources[0].removable);
}

TEST(PayloaderCaptured, ReadsTheRepositoryAndItsUpdates)
{
    PayloadList installed;
    ASSERT_TRUE(parse_payloads(captured("list_payloads"), installed));
    std::vector<RepoSource> sources;
    ASSERT_TRUE(parse_repository(captured("repository_payloads"), sources));
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_EQ(sources[0].payloads.size(), 28u);
    int self_updates = 0;
    for (const RepoPayload &p : sources[0].payloads)
    {
        const RepoStatus status = repo_status(p, sources[0].url, installed);
        if (status.self)
            self_updates += status.kind == RepoStatus::Kind::update ? 1 : 0;
    }
    EXPECT_EQ(self_updates, 1); // the repository had pldmgr v0.6.3 while 0.6.1 ran
}

} // namespace
} // namespace hui::payloader
