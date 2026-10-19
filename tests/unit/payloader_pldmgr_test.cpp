// PayLoader - Tests for pldmgr's replies, the calls PayLoader makes, repository status and the
// store. SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/pldmgr.hpp"
#include "payloader/repo_status.hpp"
#include "payloader/store.hpp"
#include "unit/payloader_fake.hpp"

#include <gtest/gtest.h>

namespace hui::payloader
{
namespace
{

using test::FakePldmgr;

bool has_header(const Request &request, const std::string &name, const std::string &value)
{
    for (const auto &[n, v] : request.headers)
        if (n == name && v == value)
            return true;
    return false;
}

// ---- Parsing ------------------------------------------------------------------------

TEST(PayloaderParse, ReadsInstalledPayloads)
{
    PayloadList list;
    ASSERT_TRUE(parse_payloads(test::kListPayloads, list));
    EXPECT_EQ(list.running_version, "0.6.1");
    ASSERT_EQ(list.items.size(), 3u);
    EXPECT_EQ(list.items[0].kind, "local");
    EXPECT_EQ(list.items[0].title(), "ftpsrv");
    EXPECT_EQ(list.items[0].origin, "web_upload");
    EXPECT_EQ(list.items[1].kind, "repo");
    EXPECT_EQ(list.items[1].slot, "repo/kstuff@1a2b3c4d");
    EXPECT_EQ(list.items[1].version, "1.2");
    EXPECT_EQ(list.items[1].description, "Fake package support \xc3\xa9");
    EXPECT_EQ(list.items[2].kind, "usb");
    EXPECT_EQ(list.items[2].slot, "/mnt/usb0/pldmgr/test.elf");
    EXPECT_EQ(list.items[2].title(), "test");
}

TEST(PayloaderParse, RefusesSomethingElse)
{
    PayloadList list;
    EXPECT_FALSE(parse_payloads("404 Not Found\n", list));
    EXPECT_FALSE(parse_payloads(R"({"items":3})", list));
    Config config;
    EXPECT_FALSE(parse_config("[]", config));
}

TEST(PayloaderParse, ReadsTheConfigWithFallbacks)
{
    Config config;
    ASSERT_TRUE(parse_config(test::kConfig, config));
    EXPECT_TRUE(config.autoload_enabled);
    EXPECT_EQ(config.autoload_delay, 5);
    EXPECT_EQ(config.autoload_list, "local/ftpsrv,!2000,repo/kstuff@1a2b3c4d");
    EXPECT_FALSE(config.auto_browser_open);
    EXPECT_FALSE(config.kill_disc_player);
    EXPECT_TRUE(config.scan_usb);
    EXPECT_FALSE(config.auto_install_app);
    EXPECT_TRUE(config.multi_sources);
    EXPECT_EQ(config.favorites, "local/ftpsrv");

    Config defaults;
    ASSERT_TRUE(parse_config(R"({"AUTOLOAD_DELAY":"x","AUTO_BROWSER_OPEN":1})", defaults));
    EXPECT_EQ(defaults.autoload_delay, 5);
    EXPECT_TRUE(defaults.auto_browser_open);
    EXPECT_TRUE(defaults.kill_disc_player);
}

TEST(PayloaderParse, ReadsAutoloadProgress)
{
    AutoloadStatus status;
    ASSERT_TRUE(parse_autoload_status(test::kAutoloadStatus, status));
    EXPECT_EQ(status.state, AutoloadState::executing);
    EXPECT_EQ(status.remaining_ms, 2500);
    EXPECT_EQ(status.total, 2);
    EXPECT_EQ(status.done, 1);
    EXPECT_EQ(status.current, "repo/kstuff@1a2b3c4d");
    EXPECT_EQ(status.outcomes, "sp");
    EXPECT_EQ(autoload_state("countdown"), AutoloadState::countdown);
    EXPECT_EQ(autoload_state("later"), AutoloadState::unknown);
}

TEST(PayloaderParse, ReadsRepositories)
{
    std::vector<RepoSource> sources;
    ASSERT_TRUE(parse_repository(test::kRepository, sources));
    ASSERT_EQ(sources.size(), 2u);
    EXPECT_EQ(sources[0].name, "Official Repository");
    ASSERT_EQ(sources[0].payloads.size(), 3u);
    EXPECT_EQ(sources[0].payloads[0].key(), "kstuff");
    EXPECT_EQ(sources[0].payloads[2].key(), "ps5-ftp-server"); // no id: the slug of the name
    EXPECT_TRUE(sources[1].error);
    EXPECT_TRUE(sources[1].payloads.empty());
}

TEST(PayloaderParse, ReadsSourcesAndProcesses)
{
    std::vector<Source> sources;
    ASSERT_TRUE(parse_sources(test::kSources, sources));
    ASSERT_EQ(sources.size(), 2u);
    EXPECT_FALSE(sources[0].removable);
    EXPECT_TRUE(sources[1].removable);

    std::vector<Process> processes;
    ASSERT_TRUE(parse_processes(test::kProcesses, processes));
    ASSERT_EQ(processes.size(), 3u);
    EXPECT_FALSE(processes[0].payload);
    EXPECT_EQ(processes[2].pid, 131);
    EXPECT_DOUBLE_EQ(processes[2].memory_mib, 4.2);
    EXPECT_TRUE(processes[2].payload);
}

TEST(PayloaderParse, ReadsInstallResults)
{
    InstallResult result;
    ASSERT_TRUE(parse_install(R"({"ok":true,"slot":"repo/x@h","id":"x","version":"1.0"})", result));
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.slot, "repo/x@h");

    ASSERT_TRUE(parse_install(
        R"({"ok":false,"conflict":{"slot":"repo/x@aa","source_name":"Other","version":"0.9"}})",
        result));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.conflict);
    EXPECT_EQ(result.conflict_slot, "repo/x@aa");
    EXPECT_EQ(result.conflict_source, "Other");

    ASSERT_TRUE(parse_install(R"({"ok":true,"self_update":true,"version":"0.6.2"})", result));
    EXPECT_TRUE(result.self_update);
    EXPECT_FALSE(result.conflict);
}

TEST(PayloaderParse, WritesOnlyTheChangedSettings)
{
    ConfigChange change;
    EXPECT_EQ(config_body(change), "{}");
    change.autoload_enabled = false;
    change.autoload_delay = 10;
    change.autoload_list = "local/a,!500";
    change.favorites = "x\"y";
    EXPECT_EQ(config_body(change), R"({"AUTOLOAD_ENABLED":false,"AUTOLOAD_DELAY":10,)"
                                   R"("AUTOLOAD_LIST":"local/a,!500","FAVORITES":"x\"y"})");
}

TEST(PayloaderParse, DescribesFailures)
{
    EXPECT_EQ(describe_failure(Reply{}), "pldmgr does not answer on port 8084");
    EXPECT_NE(describe_failure(Reply{200, "", "404 Not Found\n"}).find("does not know"),
              std::string::npos);
    EXPECT_EQ(describe_failure(Reply{403, "", R"({"error":"Mutation origin rejected"})"}),
              "pldmgr refused the change");
    EXPECT_EQ(describe_failure(Reply{400, "", R"({"ok":false,"message":"Source exists"})"}),
              "Source exists");
    EXPECT_EQ(describe_failure(Reply{500, "", "Failed to launch payload\n"}),
              "Failed to launch payload");
    EXPECT_EQ(describe_failure(Reply{500, "", "<html>"}), "pldmgr answered with status 500");
}

// ---- Calls -----------------------------------------------------------------------------

TEST(PayloaderCalls, MarksEveryChangeAndNoRead)
{
    FakePldmgr fake;
    fake.reply("POST /loadpayload:", 200, "OK");
    fake.reply("GET /list_payloads", 200, test::kListPayloads);
    Pldmgr api(fake.transport());
    PayloadList list;
    EXPECT_TRUE(api.payloads(list).ok);
    EXPECT_TRUE(api.launch("repo/k stuff@1a2b").ok);
    const auto requests = fake.requests();
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_FALSE(has_header(requests[0], "X-PLDMGR-Request", "1"));
    EXPECT_TRUE(has_header(requests[1], "X-PLDMGR-Request", "1"));
    EXPECT_EQ(requests[1].target, "/loadpayload:repo/k%20stuff@1a2b");
}

TEST(PayloaderCalls, TreatsAnUnknownRouteAsAFailure)
{
    FakePldmgr fake;
    Pldmgr api(fake.transport());
    const Outcome done = api.abort_autoload();
    EXPECT_FALSE(done.ok);
    EXPECT_FALSE(done.no_answer);
}

TEST(PayloaderCalls, ChecksPldmgrsVersion)
{
    FakePldmgr fake;
    Pldmgr api(fake.transport());
    std::string version;
    bool supported = true;
    fake.reply("GET /version", 200, "0.5.2\n");
    ASSERT_TRUE(api.version(version, supported).ok);
    EXPECT_EQ(version, "0.5.2");
    EXPECT_FALSE(supported);
    fake.reply("GET /version", 200, "0.6.1");
    ASSERT_TRUE(api.version(version, supported).ok);
    EXPECT_TRUE(supported);
    fake.set_offline(true);
    const Outcome gone = api.version(version, supported);
    EXPECT_FALSE(gone.ok);
    EXPECT_TRUE(gone.no_answer);
}

TEST(PayloaderCalls, InstallsWithTheSourceAndMode)
{
    FakePldmgr fake;
    fake.reply("POST /repository_install", 200, R"({"ok":true,"slot":"repo/ps5-ftp-server@h"})");
    Pldmgr api(fake.transport());
    RepoPayload payload;
    payload.name = "PS5 FTP-Server";
    InstallResult result;
    ASSERT_TRUE(
        api.install(payload, "https://a.b/p.json", InstallMode::replace, "repo/x@1", result).ok);
    EXPECT_EQ(fake.last("POST", "/repository_install")->target,
              "/repository_install?id=ps5-ftp-server&source_url=https%3A%2F%2Fa.b%2Fp.json"
              "&mode=replace&replace_slot=repo%2Fx%401");
    EXPECT_GE(fake.last("POST", "/repository_install")->timeout_ms, 60000);
}

TEST(PayloaderCalls, ReportsAnInstallConflictAsAnAnswer)
{
    FakePldmgr fake;
    fake.reply(
        "POST /repository_install", 200,
        R"({"ok":false,"conflict":{"slot":"repo/x@aa","source_name":"Other","version":"1"}})");
    Pldmgr api(fake.transport());
    RepoPayload payload;
    payload.id = "x";
    InstallResult result;
    EXPECT_TRUE(api.install(payload, "u", InstallMode::normal, "", result).ok);
    EXPECT_TRUE(result.conflict);

    fake.reply("POST /repository_install", 400, R"({"ok":false,"message":"Checksum mismatch"})");
    const Outcome refused = api.install(payload, "u", InstallMode::normal, "", result);
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error, "Checksum mismatch");
}

TEST(PayloaderCalls, SavesConfigAsJson)
{
    FakePldmgr fake;
    fake.reply("POST /set_config", 200, "OK");
    Pldmgr api(fake.transport());
    ConfigChange change;
    change.auto_browser_open = false;
    ASSERT_TRUE(api.save_config(change).ok);
    const auto request = fake.last("POST", "/set_config");
    EXPECT_EQ(request->body, R"({"AUTO_BROWSER_OPEN":false})");
    EXPECT_TRUE(has_header(*request, "Content-Type", "application/json"));
}

TEST(PayloaderCalls, StopsAProcessAndRemovesASource)
{
    FakePldmgr fake;
    fake.reply("POST /process_kill", 500, R"({"ok":false,"error":"Failed to kill"})");
    fake.reply("POST /sources_remove", 200, R"({"ok":true,"message":"Removed"})");
    Pldmgr api(fake.transport());
    const Outcome stopped = api.stop(131);
    EXPECT_FALSE(stopped.ok);
    EXPECT_EQ(stopped.error, "Failed to kill");
    EXPECT_EQ(fake.last("POST", "/process_kill")->target, "/process_kill?pid=131");
    EXPECT_TRUE(api.remove_source(1).ok);
}

// ---- Repository status -----------------------------------------------------------------

class RepoStatusTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        ASSERT_TRUE(parse_payloads(test::kListPayloads, installed_));
        ASSERT_TRUE(parse_repository(test::kRepository, sources_));
    }
    PayloadList installed_;
    std::vector<RepoSource> sources_;
};

TEST_F(RepoStatusTest, SeesANewerVersionFromTheOldOfficialAddress)
{
    const RepoStatus status =
        repo_status(sources_[0].payloads[0], sources_[0].url, installed_); // kstuff 1.3 over 1.2
    EXPECT_EQ(status.kind, RepoStatus::Kind::update);
    ASSERT_NE(status.installed, nullptr);
    EXPECT_EQ(status.installed->slot, "repo/kstuff@1a2b3c4d");
}

TEST_F(RepoStatusTest, SeesAnotherBuildOfTheSameVersion)
{
    RepoPayload same = sources_[0].payloads[0];
    same.version = "1.2";
    same.last_update = "2026-09-01";
    same.checksum = "abcdef";
    EXPECT_EQ(repo_status(same, sources_[0].url, installed_).kind, RepoStatus::Kind::installed);
    same.checksum = "abcdee";
    EXPECT_EQ(repo_status(same, sources_[0].url, installed_).kind, RepoStatus::Kind::update);
}

TEST_F(RepoStatusTest, SeesTheSamePayloadFromAnotherRepository)
{
    const RepoStatus status =
        repo_status(sources_[0].payloads[0], "https://example.invalid/payloads.json", installed_);
    EXPECT_EQ(status.kind, RepoStatus::Kind::not_installed);
    ASSERT_NE(status.conflict, nullptr);
    EXPECT_EQ(status.conflict->slot, "repo/kstuff@1a2b3c4d");
}

TEST_F(RepoStatusTest, ComparesPldmgrWithTheOneRunning)
{
    const RepoPayload &self = sources_[0].payloads[1];
    EXPECT_TRUE(repo_payload_visible(self, sources_[0].url));
    EXPECT_FALSE(repo_payload_visible(self, "https://example.invalid/payloads.json"));
    RepoStatus status = repo_status(self, sources_[0].url, installed_);
    EXPECT_TRUE(status.self);
    EXPECT_EQ(status.kind, RepoStatus::Kind::update); // 0.6.2 over 0.6.1
    installed_.pending_version = "0.6.2";
    EXPECT_EQ(repo_status(self, sources_[0].url, installed_).kind, RepoStatus::Kind::installed);
}

TEST_F(RepoStatusTest, LeavesLocalPayloadsAlone)
{
    // ftpsrv is installed by hand ("local"); the repository's copy is not it.
    EXPECT_EQ(repo_status(sources_[0].payloads[2], sources_[0].url, installed_).kind,
              RepoStatus::Kind::not_installed);
}

// ---- Store ------------------------------------------------------------------------------

class StoreTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        fake_.reply("GET /version", 200, "0.6.1");
        fake_.reply("GET /list_payloads", 200, test::kListPayloads);
        fake_.reply("GET /get_config", 200, test::kConfig);
        fake_.reply("GET /processes_list", 200, test::kProcesses);
        fake_.reply("POST /set_config", 200, "OK");
        fake_.reply("POST /loadpayload:", 200, "OK");
        store_ = std::make_unique<Store>([this] { return fake_.transport(); });
    }

    // One frame, then the worker's jobs, then the next frame shows them.
    void settle()
    {
        store_->update(0.016f);
        store_->wait_idle();
        store_->update(0.016f);
        store_->wait_idle();
        store_->update(0.016f);
    }

    FakePldmgr fake_;
    std::unique_ptr<Store> store_;
};

TEST_F(StoreTest, ConnectsAndReadsWhatAScreenWants)
{
    EXPECT_EQ(store_->view().link, Link::checking);
    store_->want(Topic::payloads);
    settle();
    EXPECT_EQ(store_->view().link, Link::ready);
    EXPECT_EQ(store_->view().version, "0.6.1");
    EXPECT_TRUE(store_->view().have_payloads);
    EXPECT_TRUE(store_->view().have_config);
    EXPECT_FALSE(store_->view().have_processes);
    EXPECT_EQ(store_->view().payloads.items.size(), 3u);
}

TEST_F(StoreTest, SaysWhenPldmgrIsAwayOrTooOld)
{
    fake_.set_offline(true);
    settle();
    EXPECT_EQ(store_->view().link, Link::offline);

    fake_.set_offline(false);
    fake_.reply("GET /version", 200, "0.5.3");
    store_->refresh();
    settle();
    EXPECT_EQ(store_->view().link, Link::too_old);
    EXPECT_EQ(store_->view().version, "0.5.3");
}

TEST_F(StoreTest, NoticesWhenPldmgrGoesAway)
{
    settle();
    ASSERT_EQ(store_->view().link, Link::ready);
    fake_.set_offline(true);
    store_->want(Topic::processes);
    settle();
    EXPECT_EQ(store_->view().link, Link::offline);
}

TEST_F(StoreTest, StartsAPayloadAndSaysSo)
{
    store_->want(Topic::payloads);
    settle();
    store_->launch(store_->view().payloads.items[0]);
    EXPECT_EQ(store_->view().busy.size(), 0u); // shown after the next update
    store_->update(0.0f);
    store_->wait_idle();
    store_->update(0.0f);
    EXPECT_TRUE(store_->view().busy.empty());
    const std::vector<Notice> notices = store_->take_notices();
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_FALSE(notices[0].failed);
    EXPECT_EQ(notices[0].title, "ftpsrv started");
    EXPECT_TRUE(store_->take_notices().empty());
}

TEST_F(StoreTest, KeepsOtherFavouritesWhenAddingOne)
{
    settle();
    store_->set_favourite("repo/kstuff@1a2b3c4d", true);
    store_->wait_idle();
    EXPECT_EQ(fake_.last("POST", "/set_config")->body,
              R"({"FAVORITES":"local/ftpsrv,repo/kstuff@1a2b3c4d"})");
    store_->set_favourite("local/ftpsrv", false);
    store_->wait_idle();
    // The fake still answers with the original favourites; only ftpsrv is taken out of them.
    EXPECT_EQ(fake_.last("POST", "/set_config")->body, R"({"FAVORITES":""})");
}

TEST_F(StoreTest, PassesAnInstallConflictToTheScreen)
{
    fake_.reply(
        "POST /repository_install", 200,
        R"({"ok":false,"conflict":{"slot":"repo/x@aa","source_name":"Other","version":"1"}})");
    settle();
    RepoPayload payload;
    payload.id = "x";
    payload.name = "X";
    store_->install(payload, "https://a.b/p.json", InstallMode::normal, "");
    store_->wait_idle();
    const std::vector<Notice> notices = store_->take_notices();
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_TRUE(notices[0].conflict);
    EXPECT_EQ(notices[0].conflict_slot, "repo/x@aa");
    EXPECT_EQ(notices[0].source_url, "https://a.b/p.json");
}

} // namespace
} // namespace hui::payloader
