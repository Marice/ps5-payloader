// PayLoader - Tests for the screens: what a press sends to pldmgr, and the card without it.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/concepts.hpp"
#include "payloader/store.hpp"
#include "unit/concept_fixture.hpp"
#include "unit/payloader_fake.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace
{

using hui::Action;
using hui::Direction;
using hui::app::Concept;
using hui::payloader::Reply;
using hui::payloader::Request;
using hui::payloader::Transport;
using hui::payloader::test::FakePldmgr;

// Nothing answers: what the app sees without pldmgr.
class Silent final : public Transport
{
  public:
    Reply send(const Request &) override
    {
        return {};
    }
};

class Screens : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        namespace t = hui::payloader::test;
        fake_.reply("GET /version", 200, "0.6.1");
        fake_.reply("GET /list_payloads", 200, t::kListPayloads);
        fake_.reply("GET /get_config", 200, t::kConfig);
        fake_.reply("GET /processes_list", 200, t::kProcesses);
        fake_.reply("GET /autoload_status", 200, t::kAutoloadStatus);
        fake_.reply("GET /repository_payloads", 200, t::kRepository);
        fake_.reply("GET /sources_list", 200, t::kSources);
        fake_.reply("POST /loadpayload:", 200, "OK");
        fake_.reply("POST /set_config", 200, "OK");
        fake_.reply("POST /process_kill", 200, R"({"ok":true,"message":"Killed"})");
        hui::payloader::install_store([this] { return fake_.transport(); });
    }

    void TearDown() override
    {
        // The next test (or another suite) must not reach this fake after it is gone.
        hui::payloader::install_store([] { return std::make_unique<Silent>(); });
    }

    // Frames and finished requests, until the screen shows what pldmgr said.
    void settle(Concept &screen)
    {
        for (int i = 0; i < 8; ++i)
        {
            send(screen, idle(), 0.25f);
            hui::payloader::store().wait_idle();
        }
    }

    void step(Concept &screen, const hui::InputFrame &input)
    {
        send(screen, input, 0.3f);
        hui::payloader::store().wait_idle();
        send(screen, idle(), 0.1f);
    }

    FakePldmgr fake_;
};

TEST_F(Screens, PayloadsStartsTheFocusedPayload)
{
    const std::unique_ptr<Concept> screen = hui::concepts::make_payloads(context_);
    screen->enter();
    settle(*screen);
    expect_drawable(*screen);
    step(*screen, press(Action::confirm)); // the first favourite: ftpsrv
    const auto request = fake_.last("POST", "/loadpayload:");
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->target, "/loadpayload:local/ftpsrv");
}

TEST_F(Screens, PayloadsTakesOneOutOfTheAutoloadList)
{
    const std::unique_ptr<Concept> screen = hui::concepts::make_payloads(context_);
    screen->enter();
    settle(*screen);
    step(*screen, press(Action::north)); // its menu: Start, Favourite, Start with autoload ...
    step(*screen, nav(Direction::down));
    step(*screen, nav(Direction::down));
    expect_drawable(*screen);
    step(*screen, press(Action::confirm));
    const auto request = fake_.last("POST", "/set_config");
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->body, R"({"AUTOLOAD_ENABLED":true,"AUTOLOAD_DELAY":5,)"
                             R"("AUTOLOAD_LIST":"!2000,repo/kstuff@1a2b3c4d"})");
}

TEST_F(Screens, AutoloadMovesAnEntryDown)
{
    const std::unique_ptr<Concept> screen = hui::concepts::make_autoload(context_);
    screen->enter();
    settle(*screen);
    step(*screen, nav(Direction::down)); // the countdown
    step(*screen, nav(Direction::down)); // 1. ftpsrv
    step(*screen, press(Action::confirm));
    step(*screen, nav(Direction::down)); // past "Move up", which is off for the first
    expect_drawable(*screen);
    step(*screen, press(Action::confirm));
    const auto request = fake_.last("POST", "/set_config");
    ASSERT_TRUE(request.has_value());
    EXPECT_NE(request->body.find(R"("AUTOLOAD_LIST":"!2000,local/ftpsrv,repo/kstuff@1a2b3c4d")"),
              std::string::npos)
        << request->body;
}

TEST_F(Screens, RunningStopsAPayloadAfterAsking)
{
    const std::unique_ptr<Concept> screen = hui::concepts::make_running(context_);
    screen->enter();
    settle(*screen);
    step(*screen, press(Action::confirm)); // asks first
    EXPECT_FALSE(fake_.last("POST", "/process_kill").has_value());
    step(*screen, nav(Direction::right)); // "Stop"
    step(*screen, press(Action::confirm));
    const auto request = fake_.last("POST", "/process_kill");
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->target, "/process_kill?pid=131"); // not pldmgr (120), not the system (71)
}

TEST_F(Screens, EveryScreenExplainsWhenPldmgrIsAway)
{
    fake_.set_offline(true);
    for (const hui::app::ConceptFactory factory : hui::app::concept_registry())
    {
        const std::unique_ptr<Concept> screen = factory(context_);
        screen->enter();
        settle(*screen);
        EXPECT_EQ(hui::payloader::store().view().link, hui::payloader::Link::offline);
        step(*screen, press(Action::confirm)); // "check now"
        expect_drawable(*screen);
    }
    for (const Request &request : fake_.requests())
        EXPECT_EQ(request.method, "GET") << request.target; // nothing was changed blindly
}

TEST_F(Screens, AutoloadEditsTheAutoloadersListWithoutPldmgr)
{
    namespace fs = std::filesystem;
    const std::string root = (fs::temp_directory_path() / "payloader-screens-chain").string();
    fs::remove_all(root);
    fs::create_directories(root + "/data/ps5_autoloader");
    std::ofstream(root + "/data/ps5_autoloader/autoload.txt")
        << "websrv.elf\n!3000\netaHEN.elf\n!12000\nps5-signal.elf\n";
    fake_.set_offline(true);
    hui::payloader::install_store([this] { return fake_.transport(); }, root);

    const std::unique_ptr<Concept> screen = hui::concepts::make_autoload(context_);
    screen->enter();
    settle(*screen);
    EXPECT_EQ(hui::payloader::store().view().chain.kind, hui::payloader::ChainKind::autoloader);
    expect_drawable(*screen);
    step(*screen,
         press(Action::confirm)); // the focus starts on 1. websrv (the folder line is read-only)
    step(*screen, nav(Direction::down)); // past "Move up", off for the first
    step(*screen, press(Action::confirm));
    settle(*screen);

    std::stringstream text;
    text << std::ifstream(root + "/data/ps5_autoloader/autoload.txt").rdbuf();
    EXPECT_EQ(text.str(), "!3000\nwebsrv.elf\netaHEN.elf\n!12000\nps5-signal.elf\n");
    EXPECT_TRUE(fs::exists(root + "/data/ps5_autoloader/autoload.txt.bak-payloader-" +
                           hui::payloader::today()));
    fs::remove_all(root);
}

} // namespace
