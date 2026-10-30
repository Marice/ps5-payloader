// PayLoader - Tests for the Signal suite: its order, its source, installing it and starting it.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/launcher.hpp"
#include "payloader/store.hpp"
#include "payloader/suite.hpp"
#include "unit/payloader_fake.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace hui::payloader
{
namespace
{

namespace fs = std::filesystem;
using test::FakePldmgr;

// The suite repository as pldmgr serves it in /repository_payloads (shortened).
std::string suite_repository(const std::string &exporter_version)
{
    return std::string(
               R"({"sources":[{"id":"default","name":"Official Repository","url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","error":false,"payloads":[]},)") +
           R"({"id":"s1","name":"Signal suite","url":")" + kSuiteUrl +
           R"(","error":false,"payloads":[)" +
           R"({"id":"ps5-exporter","name":"ps5-exporter","filename":"ps5-exporter.elf","version":")" +
           exporter_version + R"(","checksum":"aa"},)" +
           R"({"id":"ps5-homeassistant","name":"ps5-homeassistant","filename":"ps5-homeassistant.elf","version":"v0.2.1","checksum":"bb"},)" +
           R"({"id":"ps5-signal","name":"ps5-signal","filename":"ps5-signal.elf","version":"v0.2.1","checksum":"cc"}]}]})";
}

// Installed from the suite, as /list_payloads shows them.
std::string installed_suite(const std::string &root_hint)
{
    (void)root_hint;
    std::string items;
    const char *ids[] = {"ps5-exporter", "ps5-homeassistant", "ps5-signal"};
    const char *versions[] = {"v0.3.1", "v0.2.1", "v0.2.1"};
    for (int i = 0; i < 3; ++i)
    {
        if (i)
            items += ",";
        items += std::string(R"({"slot":"repo/)") + ids[i] +
                 R"(@5u1t3000","path":"/data/pldmgr/payloads/repo/)" + ids[i] + "@5u1t3000/" +
                 ids[i] + R"(.elf","kind":"repo","filename":")" + ids[i] + R"(.elf","id":")" +
                 ids[i] + R"(","source_url":")" + kSuiteUrl + R"(","name":")" + ids[i] +
                 R"(","version":")" + versions[i] + R"(","checksum":""})";
    }
    return R"({"self":{"id":"pldmgr","running_version":"0.6.1","pending_version":""},"items":[)" +
           items + R"(],"usb":[]})";
}

TEST(PayloaderSuite, KnowsItsPayloadsByAnyName)
{
    EXPECT_EQ(suite_rank("ps5-exporter"), 0);
    EXPECT_EQ(suite_rank("ps5-homeassistant.elf"), 1);
    EXPECT_EQ(suite_rank("repo/ps5-signal@1a2b3c4d"), 2);
    EXPECT_EQ(suite_rank("local/ps5-exporter"), 0);
    EXPECT_EQ(suite_rank("ps5-exporter-dev.elf"), -1);
    EXPECT_EQ(suite_rank("etaHEN.elf"), -1);
    EXPECT_TRUE(is_suite_url(std::string(kSuiteUrl) + "/"));
}

// The suite row counts only the three: another payload in the suite's repository (ps5-smbd) neither
// makes it "Installed" nor "Update ready", since "Install the whole suite" would not touch it.
TEST(PayloaderSuite, ProgressCountsOnlyTheThree)
{
    const auto offered =
        [](const std::string &id, const std::string &version, const std::string &sum)
    {
        RepoPayload p;
        p.id = id;
        p.name = id;
        p.filename = id + ".elf";
        p.version = version;
        p.checksum = sum;
        return p;
    };
    const auto have = [](const std::string &id, const std::string &version, const std::string &sum)
    {
        Payload p;
        p.slot = "repo/" + id + "@1a2b3c4d";
        p.kind = "repo";
        p.id = id;
        p.name = id;
        p.filename = id + ".elf";
        p.version = version;
        p.checksum = sum;
        p.source_url = kSuiteUrl;
        return p;
    };
    RepoSource suite;
    suite.url = kSuiteUrl;
    suite.payloads = {offered("ps5-exporter", "v0.3.1", "aa"),
                      offered("ps5-homeassistant", "v0.3.0", "bb"),
                      offered("ps5-signal", "v0.2.1", "cc"), offered("ps5-smbd", "v0.2.0", "dd")};
    PayloadList installed;
    installed.items = {have("ps5-exporter", "v0.3.1", "aa"), have("ps5-signal", "v0.2.1", "cc"),
                       have("ps5-smbd", "v0.1.0", "d0")};

    SuiteProgress progress = suite_progress(suite, installed);
    EXPECT_EQ(progress.installed,
              2); // ps5-homeassistant is missing; ps5-smbd does not stand in for it
    EXPECT_FALSE(progress.update); // ps5-smbd's update is not the suite's

    installed.items.push_back(have("ps5-homeassistant", "v0.3.0", "bb"));
    progress = suite_progress(suite, installed);
    EXPECT_EQ(progress.installed, 3);
    EXPECT_FALSE(progress.update);

    installed.items[0] = have("ps5-exporter", "v0.3.0", "a0");
    progress = suite_progress(suite, installed);
    EXPECT_EQ(progress.installed, 2);
    EXPECT_TRUE(progress.update);
}

TEST(PayloaderSuite, PlacesPayloadsInSuiteOrder)
{
    // Like Signal Companion: before the first suite payload that comes after it, with a wait.
    std::vector<AutoloadEntry> list = parse_autoload_lines("etaHEN.elf\n!12000\nps5-signal.elf\n");
    list = with_suite_entry(list, "ps5-exporter.elf");
    list = with_suite_entry(list, "ps5-homeassistant.elf");
    std::string text;
    ASSERT_TRUE(format_autoload_lines(list, text));
    EXPECT_EQ(text, "etaHEN.elf\n!12000\nps5-exporter.elf\n!3000\nps5-homeassistant.elf\n!3000\n"
                    "ps5-signal.elf\n");
    // Last, after a wait; and once only.
    list = with_suite_entry(parse_autoload_lines("websrv.elf\n"), "ps5-signal.elf");
    list = with_suite_entry(list, "ps5-signal.elf");
    ASSERT_TRUE(format_autoload_lines(list, text));
    EXPECT_EQ(text, "websrv.elf\n!3000\nps5-signal.elf\n");
}

class SuiteStoreTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        root_ = (fs::temp_directory_path() /
                 ("payloader-suite-" +
                  std::string(::testing::UnitTest::GetInstance()->current_test_info()->name())))
                    .string();
        fs::remove_all(root_);
        fs::create_directories(root_ + "/app-data");
        set_payloader_paths(root_ + "/assets",
                            root_ + "/app-data"); // the front-end marker goes here
        fake_.reply("GET /version", 200, "0.6.1");
        fake_.reply(
            "GET /get_config", 200,
            R"({"AUTOLOAD_ENABLED":false,"AUTOLOAD_LIST":"","MULTI_SOURCES_ENABLED":false})");
        fake_.reply(
            "GET /sources_list", 200,
            R"({"sources":[{"id":"default","name":"Official Repository","url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","removable":false}]})");
        fake_.reply("POST /sources_add", 200, R"({"ok":true,"name":"Signal suite"})");
        fake_.reply("POST /set_config", 200, "OK");
        fake_.reply("GET /repository_payloads", 200, suite_repository("v0.3.1"));
        fake_.reply("GET /list_payloads", 200,
                    R"({"self":{"id":"pldmgr","running_version":"0.6.1"},"items":[],"usb":[]})");
        fake_.reply("POST /repository_install", 200, R"({"ok":true,"slot":"repo/x@5u1t3000"})");
        store_ = std::make_unique<Store>([this] { return fake_.transport(); }, root_);
    }
    void TearDown() override
    {
        store_.reset();
        fs::remove_all(root_);
    }
    void settle()
    {
        for (int i = 0; i < 4; ++i)
        {
            store_->update(0.5f);
            store_->wait_idle();
        }
    }
    std::string read(const std::string &path)
    {
        std::stringstream text;
        text << std::ifstream(root_ + path).rdbuf();
        return text.str();
    }
    void write(const std::string &path, const std::string &text)
    {
        fs::create_directories(fs::path(root_ + path).parent_path());
        std::ofstream(root_ + path) << text;
    }

    FakePldmgr fake_;
    std::string root_;
    std::unique_ptr<Store> store_;
};

TEST_F(SuiteStoreTest, AddsTheSuiteSourceAndShowsExtraSources)
{
    settle();
    const auto added = fake_.last("POST", "/sources_add");
    ASSERT_TRUE(added.has_value());
    EXPECT_NE(added->target.find("ps5-signal-suite"), std::string::npos);
    EXPECT_EQ(fake_.last("POST", "/set_config")->body, R"({"MULTI_SOURCES_ENABLED":true})");
    // PayLoader also takes over pldmgr's front-end on connecting; the suite has its own notice.
    bool told = false;
    for (const Notice &n : store_->take_notices())
        told = told || n.title == "Signal suite added to pldmgr";
    EXPECT_TRUE(told);
}

TEST_F(SuiteStoreTest, InstallsTheSuiteInOrderAndThenAsks)
{
    settle();
    store_->take_notices();
    store_->install_suite();
    store_->wait_idle();
    std::vector<std::string> installed;
    for (const auto &request : fake_.requests())
        if (request.target.rfind("/repository_install?id=", 0) == 0)
            installed.push_back(request.target.substr(23, request.target.find('&') - 23));
    EXPECT_EQ(installed,
              (std::vector<std::string>{"ps5-exporter", "ps5-homeassistant", "ps5-signal"}));
    const std::vector<Notice> notices = store_->take_notices();
    ASSERT_FALSE(notices.empty());
    EXPECT_TRUE(notices.back().suite_installed);
}

TEST_F(SuiteStoreTest, SkipsWhatIsUpToDate)
{
    fake_.reply("GET /list_payloads", 200, installed_suite(root_));
    fake_.reply("GET /repository_payloads", 200,
                suite_repository("v0.3.2")); // only the exporter is newer
    settle();
    store_->install_suite();
    store_->wait_idle();
    int installs = 0;
    for (const auto &request : fake_.requests())
        installs += request.target.rfind("/repository_install?id=ps5-exporter", 0) == 0 ? 1 : 0;
    EXPECT_EQ(installs, 1);
    EXPECT_EQ(fake_.last("POST", "/repository_install")->target.find("id=ps5-signal"),
              std::string::npos);
}

TEST_F(SuiteStoreTest, AddsTheSuiteToTheAutoloadersList)
{
    write("/data/ps5_autoloader/autoload.txt", "etaHEN.elf\n!12000\nps5-signal.elf\n");
    write("/data/ps5_autoloader/ps5-signal.elf", "OLD SIGNAL");
    for (const char *id : {"ps5-exporter", "ps5-homeassistant", "ps5-signal"})
        write(std::string("/data/pldmgr/payloads/repo/") + id + "@5u1t3000/" + id + ".elf",
              std::string("NEW ") + id);
    fake_.reply("GET /list_payloads", 200, installed_suite(root_));
    settle();
    store_->add_suite_to_chain();
    store_->wait_idle();
    // pldmgr's folders are readable for pldmgr only: the list names their full path, and the
    // signal copy that was listed already keeps its place and its file.
    EXPECT_EQ(
        read("/data/ps5_autoloader/autoload.txt"),
        "etaHEN.elf\n!12000\n/data/pldmgr/payloads/repo/ps5-exporter@5u1t3000/ps5-exporter.elf\n"
        "!3000\n/data/pldmgr/payloads/repo/ps5-homeassistant@5u1t3000/ps5-homeassistant.elf\n"
        "!3000\nps5-signal.elf\n");
    EXPECT_EQ(read("/data/ps5_autoloader/ps5-signal.elf"), "OLD SIGNAL");
    EXPECT_FALSE(fs::exists(root_ + "/data/ps5_autoloader/ps5-exporter.elf"));
    EXPECT_TRUE(fs::exists(root_ + "/data/ps5_autoloader/autoload.txt.bak-payloader-" + today()));
}

TEST_F(SuiteStoreTest, AddsTheSuiteToPldmgrsListWithoutAutoloader)
{
    fs::create_directories(root_ + "/data/pldmgr"); // /data readable, no autoloader list
    fake_.reply("GET /list_payloads", 200, installed_suite(root_));
    fake_.reply(
        "GET /get_config", 200,
        R"({"AUTOLOAD_ENABLED":true,"AUTOLOAD_LIST":"local/etahen,!10000","MULTI_SOURCES_ENABLED":true})");
    settle();
    store_->add_suite_to_chain();
    store_->wait_idle();
    EXPECT_EQ(fake_.last("POST", "/set_config")->body,
              R"({"AUTOLOAD_LIST":"local/etahen,!10000,repo/ps5-exporter@5u1t3000,!3000,)"
              R"(repo/ps5-homeassistant@5u1t3000,!3000,repo/ps5-signal@5u1t3000"})");
}

} // namespace
} // namespace hui::payloader
