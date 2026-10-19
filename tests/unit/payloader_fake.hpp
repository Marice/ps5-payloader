// PayLoader - A pldmgr stand-in for tests: canned replies per route, every request kept.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "payloader/http.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hui::payloader::test
{

// Replies by "METHOD target-without-query"; anything else gets pldmgr's
// "200 with 404 body". offline makes every request go unanswered.
class FakePldmgr
{
  public:
    void reply(const std::string &route, int status, std::string body)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Reply r;
        r.status = status;
        r.body = std::move(body);
        replies_[route] = std::move(r);
    }

    void set_offline(bool offline)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        offline_ = offline;
    }

    std::vector<Request> requests() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }

    std::optional<Request> last(const std::string &method, const std::string &path_prefix) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = requests_.rbegin(); it != requests_.rend(); ++it)
            if (it->method == method && it->target.rfind(path_prefix, 0) == 0)
                return *it;
        return std::nullopt;
    }

    Reply answer(const Request &request)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(request);
        if (offline_)
            return {};
        std::string path = request.target.substr(0, request.target.find('?'));
        if (path.rfind("/loadpayload:", 0) == 0)
            path = "/loadpayload:";
        const auto found = replies_.find(request.method + " " + path);
        if (found != replies_.end())
            return found->second;
        Reply missing;
        missing.status = 200;
        missing.body = "404 Not Found\n";
        return missing;
    }

    std::unique_ptr<Transport> transport()
    {
        class Link final : public Transport
        {
          public:
            explicit Link(FakePldmgr &fake) : fake_(fake)
            {
            }
            Reply send(const Request &request) override
            {
                return fake_.answer(request);
            }

          private:
            FakePldmgr &fake_;
        };
        return std::make_unique<Link>(*this);
    }

  private:
    mutable std::mutex mutex_;
    std::map<std::string, Reply> replies_;
    std::vector<Request> requests_;
    bool offline_ = false;
};

// Replies as pldmgr v0.6.1 writes them (src/payload_mgr.c, http_server.c,
// process_mgr.c, sources.c), shortened.
inline constexpr const char *kListPayloads = R"({
  "self":{"id":"pldmgr","running_version":"0.6.1","pending_version":""},
  "items": [
    {
      "slot": "local/ftpsrv",
      "path": "/data/pldmgr/payloads/local/ftpsrv/ftpsrv.elf",
      "kind": "local",
      "filename": "ftpsrv.elf",
      "origin": "web_upload",
      "origin_detail": ""
    },
    {
      "slot": "repo/kstuff@1a2b3c4d",
      "path": "/data/pldmgr/payloads/repo/kstuff@1a2b3c4d/kstuff.elf",
      "kind": "repo",
      "filename": "kstuff.elf",
      "id": "kstuff",
      "source_url": "https://itsplk.github.io/ps5-payloads-mirror/payloads.json",
      "source_name": "Official Repository",
      "name": "kstuff",
      "version": "1.2",
      "checksum": "ABCDEF",
      "last_update": "2026-09-01",
      "author": "EchoStretch",
      "details_url": "",
      "source": "",
      "description": "Fake package support é"
    }
  ],
  "usb": [
    "/mnt/usb0/pldmgr/test.elf"
  ],
  "payloads": [
    "/data/pldmgr/payloads/local/ftpsrv/ftpsrv.elf",
    "/data/pldmgr/payloads/repo/kstuff@1a2b3c4d/kstuff.elf",
    "/mnt/usb0/pldmgr/test.elf"
  ]
}
)";

inline constexpr const char *kConfig =
    R"({"AUTOLOAD_ENABLED":true,"AUTOLOAD_LIST":"local/ftpsrv,!2000,repo/kstuff@1a2b3c4d",)"
    R"("LAST_REPOSITORY_UPDATE":0,"AUTO_BROWSER_OPEN":false,"AUTOLOAD_DELAY":5,)"
    R"("KILL_DISC_PLAYER_ON_STARTUP":false,"SCAN_USB_PAYLOADS":true,"AUTO_INSTALL_APP":false,)"
    R"("MULTI_SOURCES_ENABLED":true,"FAVORITES":"local/ftpsrv","FAVORITES_INITIALIZED":true})";

inline constexpr const char *kAutoloadStatus =
    R"({"remaining":3,"remaining_ms":2500,"total":2,"done":1,"current":"repo/kstuff@1a2b3c4d",)"
    R"("list":"local/ftpsrv,!2000,repo/kstuff@1a2b3c4d","delay":5,"KILL_DISC_PLAYER_ON_STARTUP":false,)"
    R"("SCAN_USB_PAYLOADS":false,"state":"executing","successful":1,"failed":0,"skipped":0,)"
    R"("outcomes":"sp","entry_index":1,"entry_duration_ms":0,"entry_remaining_ms":0})";

inline constexpr const char *kRepository = R"({"sources":[
  {"id":"default","name":"Official Repository","url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","last_update":1760000000,"error":false,"icons_revision":"1","payloads":[
    {"id":"kstuff","name":"kstuff","filename":"kstuff.elf","description":"Fake package support","version":"1.3","checksum":"123456","last_update":"2026-10-01","url":"https://example.invalid/kstuff.elf","source":"","source_direct":"","source_id":"default","source_name":"Official Repository","category":"System","author":"EchoStretch","details_url":"","release_notes":"","icon_key":""},
    {"id":"pldmgr","name":"Payload Manager","filename":"pldmgr.elf","description":"","version":"0.6.2","checksum":"","last_update":"","url":"","source":"","source_direct":"","source_id":"default","source_name":"Official Repository","category":"System","author":"itsPLK","details_url":"","release_notes":"","icon_key":""},
    {"id":"","name":"PS5 FTP-Server","filename":"ftpsrv.elf","description":"","version":"0.9","checksum":"","last_update":"","url":"","source":"","source_direct":"","source_id":"default","source_name":"Official Repository","category":"Tools","author":"","details_url":"","release_notes":"","icon_key":""}
  ]},
  {"id":"s1","name":"Example","url":"https://example.invalid/payloads.json","last_update":0,"error":true,"icons_revision":"","payloads":[]}
]}
)";

inline constexpr const char *kSources =
    R"({"sources":[)"
    R"({"id":"default","name":"Official Repository","url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","removable":false},)"
    R"({"id":"s1","name":"Example","url":"https://example.invalid/payloads.json","removable":true}]})";

inline constexpr const char *kProcesses = R"({"processes":[
  {"pid":71,"name":"SceShellUI","memory":512.0,"is_daemon":false},
  {"pid":120,"name":"pldmgr.elf","memory":6.2,"is_daemon":true},
  {"pid":131,"name":"ftpsrv.elf","memory":4.2,"is_daemon":true}
]}
)";

} // namespace hui::payloader::test
