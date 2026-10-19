// PayLoader - Tests for the HTTP client, the JSON reader, version order and the autoload list.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/autoload_list.hpp"
#include "payloader/http.hpp"
#include "payloader/json.hpp"
#include "payloader/versions.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>

namespace hui::payloader
{
namespace
{

// ---- JSON -----------------------------------------------------------------------

TEST(PayloaderJson, DecodesUnicodeEscapesToUtf8)
{
    JsonValue root;
    ASSERT_TRUE(parse_json(R"({"a":"café","b":"€","c":"😀"})", root));
    EXPECT_EQ(root["a"].as_string(), "caf\xc3\xa9");
    EXPECT_EQ(root["b"].as_string(), "\xe2\x82\xac");
    EXPECT_EQ(root["c"].as_string(), "\xf0\x9f\x98\x80");
}

TEST(PayloaderJson, RefusesLoneSurrogates)
{
    JsonValue root;
    EXPECT_FALSE(parse_json(R"({"a":"\ud83d"})", root));
    EXPECT_FALSE(parse_json(R"({"a":"\ude00"})", root));
    EXPECT_FALSE(parse_json(R"({"a":"\ud83dA"})", root));
}

// ---- HTTP -----------------------------------------------------------------------

TEST(PayloaderHttp, FormatsAPostWithItsHeaders)
{
    Request request;
    request.method = "POST";
    request.target = "/abort";
    request.headers.emplace_back("X-PLDMGR-Request", "1");
    const std::string wire = format_request(request);
    EXPECT_EQ(wire, "POST /abort HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n"
                    "X-PLDMGR-Request: 1\r\nContent-Length: 0\r\n\r\n");
}

TEST(PayloaderHttp, FormatsAGetWithoutALength)
{
    Request request;
    request.method = "GET";
    request.target = "/version";
    EXPECT_EQ(format_request(request),
              "GET /version HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
}

TEST(PayloaderHttp, ReadsAReplyWithALength)
{
    Reply reply;
    ASSERT_TRUE(parse_reply("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n"
                            "\r\n0.6.1 trailing",
                            reply));
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(reply.content_type, "text/plain");
    EXPECT_EQ(reply.body, "0.6.1");
}

TEST(PayloaderHttp, ReadsAChunkedReply)
{
    Reply reply;
    ASSERT_TRUE(parse_reply("HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\n\r\n"
                            "4\r\n{\"a\"\r\n3;x=y\r\n:1}\r\n0\r\n\r\n",
                            reply));
    EXPECT_EQ(reply.body, "{\"a\":1}");
}

TEST(PayloaderHttp, ReadsAReplyThatEndsAtTheClose)
{
    Reply reply;
    ASSERT_TRUE(parse_reply("HTTP/1.0 403 Forbidden\r\n\r\n{\"error\":\"no\"}", reply));
    EXPECT_EQ(reply.status, 403);
    EXPECT_EQ(reply.body, "{\"error\":\"no\"}");
}

TEST(PayloaderHttp, RefusesIncompleteMalformedAndOversizedReplies)
{
    Reply reply;
    EXPECT_FALSE(parse_reply("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort", reply));
    EXPECT_FALSE(
        parse_reply("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nab", reply));
    EXPECT_FALSE(parse_reply("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n", reply));
    EXPECT_FALSE(parse_reply("SSH-2.0-OpenSSH\r\n\r\n", reply));
    EXPECT_FALSE(parse_reply("HTTP/1.1 999 Odd\r\n\r\n", reply));
    EXPECT_FALSE(parse_reply("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789", reply, 9));
    EXPECT_FALSE(parse_reply("HTTP/1.1 200 OK", reply));
    EXPECT_EQ(reply.status, -1);
}

TEST(PayloaderHttp, EncodesQueryValues)
{
    EXPECT_EQ(url_encode("repo/kstuff@1a2b"), "repo%2Fkstuff%401a2b");
    EXPECT_EQ(url_encode("a b&c=d~e.f_g-h"), "a%20b%26c%3Dd~e.f_g-h");
    EXPECT_EQ(url_encode("\xc3\xa9"), "%C3%A9");
}

// A one-shot server on 127.0.0.1 that answers with fixed bytes, or not at all.
class OneShotServer
{
  public:
    explicit OneShotServer(std::string answer, bool answer_at_all = true)
        : answer_(std::move(answer)), answer_at_all_(answer_at_all)
    {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(fd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
        socklen_t size = sizeof(address);
        getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &size);
        port_ = ntohs(address.sin_port);
        listen(fd_, 1);
        thread_ = std::thread([this] { serve(); });
    }
    ~OneShotServer()
    {
        thread_.join();
        close(fd_);
    }
    int port() const
    {
        return port_;
    }
    const std::string &received() const
    {
        return received_;
    }

  private:
    void serve()
    {
        const int client = accept(fd_, nullptr, nullptr);
        if (client < 0)
            return;
        char buffer[4096];
        const ssize_t n = recv(client, buffer, sizeof(buffer), 0);
        if (n > 0)
            received_.assign(buffer, static_cast<std::size_t>(n));
        if (answer_at_all_)
            send(client, answer_.data(), answer_.size(), 0);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
        close(client);
    }

    std::string answer_;
    bool answer_at_all_;
    int fd_ = -1;
    int port_ = 0;
    std::string received_;
    std::thread thread_;
};

TEST(PayloaderHttp, TalksToAServerOnLoopback)
{
    OneShotServer server(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nOK\r\n0\r\n\r\n");
    Request request;
    request.method = "POST";
    request.target = "/loadpayload:local/ftpsrv";
    request.headers.emplace_back("X-PLDMGR-Request", "1");
    const Reply reply = make_loopback_transport(server.port())->send(request);
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(reply.body, "OK");
    // received() is read after the transport finished; the server thread wrote it before closing.
    EXPECT_NE(server.received().find("X-PLDMGR-Request: 1\r\n"), std::string::npos);
}

TEST(PayloaderHttp, GivesUpAtTheTimeout)
{
    OneShotServer server("", false);
    Request request;
    request.method = "GET";
    request.target = "/version";
    request.timeout_ms = 150;
    const auto start = std::chrono::steady_clock::now();
    const Reply reply = make_loopback_transport(server.port())->send(request);
    const auto took = std::chrono::steady_clock::now() - start;
    EXPECT_EQ(reply.status, -1);
    EXPECT_LT(took, std::chrono::milliseconds(500));
}

TEST(PayloaderHttp, ReportsNoAnswerWhenNothingListens)
{
    // A port that was free a moment ago and has no listener now.
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)), 0);
    socklen_t size = sizeof(address);
    getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size);
    close(fd);
    Request request{"GET", "/version", {}, {}, 2000};
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(make_loopback_transport(ntohs(address.sin_port))->send(request).status, -1);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
}

// ---- Versions ---------------------------------------------------------------------

TEST(PayloaderVersions, OrdersAsPldmgrDoes)
{
    EXPECT_EQ(compare_versions("1.10", "1.9"), 1);
    EXPECT_EQ(compare_versions("v1.2", "1.2"), 0);
    EXPECT_EQ(compare_versions("1.7", "1.7beta3"), 1);
    EXPECT_EQ(compare_versions("1.0.1", "1.0"), 1);
    EXPECT_EQ(compare_versions("1.0", "1.0.1"), -1);
    EXPECT_EQ(compare_versions("1.0a", "1.0B"), -1);
    EXPECT_EQ(compare_versions("1.02", "1.2"), 0);
    EXPECT_EQ(compare_versions("2", "1.9.9"), 1);
    EXPECT_EQ(compare_versions("", "1"), -1);
    EXPECT_EQ(compare_versions("1.0", "1.0", "2026-10-02", "2026-10-01"), 1);
    EXPECT_EQ(compare_versions("1.0", "1.0", "", "2026-10-01"), 0);
}

TEST(PayloaderVersions, MakesSlugsAsPldmgrDoes)
{
    EXPECT_EQ(slug("PS5 FTP-Server"), "ps5-ftp-server");
    EXPECT_EQ(slug("  kstuff_lite.v2!! "), "kstuff_lite.v2");
    EXPECT_EQ(slug("caf\xc3\xa9"), "caf");
    EXPECT_EQ(slug("!!!"), "payload");
}

TEST(PayloaderVersions, KnowsPldmgrsOwnNames)
{
    EXPECT_TRUE(is_system_payload("pldmgr_v0.6.1.elf"));
    EXPECT_TRUE(is_system_payload("/data/x/PLDMGR.elf"));
    EXPECT_TRUE(is_system_payload("PS5 Payload Manager"));
    EXPECT_FALSE(is_system_payload("ftpsrv.elf"));
    EXPECT_FALSE(is_system_payload(""));
}

TEST(PayloaderVersions, ChecksTheMinimumVersion)
{
    EXPECT_TRUE(version_at_least("0.6.0", 0, 6, 0));
    EXPECT_TRUE(version_at_least("0.6.1", 0, 6, 0));
    EXPECT_TRUE(version_at_least("v1.0", 0, 6, 0));
    EXPECT_TRUE(version_at_least("0.6", 0, 6, 0));
    EXPECT_FALSE(version_at_least("0.5.9", 0, 6, 0));
    EXPECT_FALSE(version_at_least("0.6beta", 0, 6, 0));
    EXPECT_FALSE(version_at_least("404 Not Found", 0, 6, 0));
    EXPECT_FALSE(version_at_least("", 0, 0, 0));
}

// ---- Autoload list ----------------------------------------------------------------

TEST(PayloaderAutoload, ReadsTheList)
{
    const auto list = parse_autoload_list(" local/a , !2000,,repo/b@h,!x,!3600001,! ");
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0], AutoloadEntry::payload("local/a"));
    EXPECT_EQ(list[1], AutoloadEntry::pause(2000));
    EXPECT_EQ(list[2], AutoloadEntry::payload("repo/b@h"));
}

TEST(PayloaderAutoload, WritesTheList)
{
    std::string text;
    ASSERT_TRUE(format_autoload_list({AutoloadEntry::payload("local/a"), AutoloadEntry::pause(0),
                                      AutoloadEntry::payload("/mnt/usb0/x.elf")},
                                     text));
    EXPECT_EQ(text, "local/a,!0,/mnt/usb0/x.elf");
    ASSERT_TRUE(format_autoload_list({}, text));
    EXPECT_EQ(text, "");
}

TEST(PayloaderAutoload, RefusesWhatPldmgrCannotKeep)
{
    std::string text = "unchanged";
    EXPECT_FALSE(format_autoload_list({AutoloadEntry::payload("a,b")}, text));
    EXPECT_FALSE(format_autoload_list({AutoloadEntry::payload("a\tb")}, text));
    EXPECT_FALSE(format_autoload_list({AutoloadEntry::payload(" ")}, text));
    EXPECT_FALSE(format_autoload_list({AutoloadEntry::payload("!1")}, text));
    EXPECT_FALSE(format_autoload_list({AutoloadEntry::pause(kMaxWaitMs + 1)}, text));
    EXPECT_EQ(text, "unchanged");

    std::vector<AutoloadEntry> long_list;
    std::string fits;
    while (fits.size() + 9 <= kMaxAutoloadBytes)
    {
        long_list.push_back(AutoloadEntry::payload("local/ab"));
        ASSERT_TRUE(format_autoload_list(long_list, fits));
    }
    long_list.push_back(AutoloadEntry::payload("local/ab"));
    EXPECT_FALSE(format_autoload_list(long_list, text));
}

TEST(PayloaderAutoload, MovesEntries)
{
    std::vector<AutoloadEntry> list = {AutoloadEntry::payload("a"), AutoloadEntry::pause(1),
                                       AutoloadEntry::payload("b")};
    EXPECT_FALSE(move_entry(list, 0, -1));
    EXPECT_FALSE(move_entry(list, 2, 1));
    EXPECT_FALSE(move_entry(list, 5, 1));
    EXPECT_TRUE(move_entry(list, 2, -1));
    EXPECT_EQ(list[1], AutoloadEntry::payload("b"));
    EXPECT_EQ(list[2], AutoloadEntry::pause(1));
    EXPECT_TRUE(autoload_contains(list, "a"));
    EXPECT_FALSE(autoload_contains(list, "c"));
}

} // namespace
} // namespace hui::payloader
