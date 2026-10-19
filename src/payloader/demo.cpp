// PayLoader - A pretend pldmgr with a few payloads, for the PC preview.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/demo.hpp"

#include <string_view>

namespace hui::payloader
{

namespace
{

// Shaped as pldmgr 0.6.1 answers. Names, versions and sizes are made up for the preview,
// so a screenshot of it never passes for real data about a real project.
constexpr std::string_view kPayloads = R"({
  "self":{"id":"pldmgr","running_version":"0.6.1","pending_version":""},
  "items":[
    {"slot":"repo/package-helper@1a2b3c4d","path":"/data/pldmgr/payloads/repo/package-helper@1a2b3c4d/package-helper.elf","kind":"repo","filename":"package-helper.elf","id":"package-helper","source_url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","source_name":"Official Repository","name":"Package Helper","version":"1.5","checksum":"aa","last_update":"2026-09-20","author":"","details_url":"","source":"","description":"Lets the console install and run fake packages."},
    {"slot":"repo/ftpsrv@1a2b3c4d","path":"/data/pldmgr/payloads/repo/ftpsrv@1a2b3c4d/ftpsrv.elf","kind":"repo","filename":"ftpsrv.elf","id":"ftpsrv","source_url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","source_name":"Official Repository","name":"FTP Server","version":"0.11","checksum":"bb","last_update":"2026-08-02","author":"","details_url":"","source":"","description":"An FTP server on port 2121 for copying files to and from the console."},
    {"slot":"repo/shsrv@1a2b3c4d","path":"/data/pldmgr/payloads/repo/shsrv@1a2b3c4d/shsrv.elf","kind":"repo","filename":"shsrv.elf","id":"shsrv","source_url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","source_name":"Official Repository","name":"Shell Server","version":"0.14","checksum":"cc","last_update":"2026-07-11","author":"","details_url":"","source":"","description":"A telnet shell on port 2323."},
    {"slot":"local/ps5-signal","path":"/data/pldmgr/payloads/local/ps5-signal/ps5-signal.elf","kind":"local","filename":"ps5-signal.elf","origin":"web_upload","origin_detail":""},
    {"slot":"local/ps5-exporter","path":"/data/pldmgr/payloads/local/ps5-exporter/ps5-exporter.elf","kind":"local","filename":"ps5-exporter.elf","origin":"web_upload","origin_detail":""}
  ],
  "usb":["/mnt/usb0/pldmgr/test-build.elf"]
})";

constexpr std::string_view kConfig =
    R"({"AUTOLOAD_ENABLED":true,"AUTOLOAD_LIST":"repo/package-helper@1a2b3c4d,!2000,repo/ftpsrv@1a2b3c4d,local/ps5-signal",)"
    R"("AUTO_BROWSER_OPEN":false,"AUTOLOAD_DELAY":5,"KILL_DISC_PLAYER_ON_STARTUP":true,)"
    R"("SCAN_USB_PAYLOADS":true,"AUTO_INSTALL_APP":false,"MULTI_SOURCES_ENABLED":true,)"
    R"("FAVORITES":"repo/ftpsrv@1a2b3c4d,local/ps5-signal"})";

constexpr std::string_view kAutoload =
    R"({"remaining":0,"remaining_ms":0,"total":3,"done":3,"current":"","state":"done",)"
    R"("successful":3,"failed":0,"skipped":0,"outcomes":"sss"})";

constexpr std::string_view kRepository = R"({"sources":[
  {"id":"default","name":"Official Repository","url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","error":false,"payloads":[
    {"id":"package-helper","name":"Package Helper","filename":"package-helper.elf","description":"Lets the console install and run fake packages.","version":"1.6","checksum":"ab","last_update":"2026-10-12","category":"System","author":"","release_notes":"Example release notes: what changed in this version."},
    {"id":"ftpsrv","name":"FTP Server","filename":"ftpsrv.elf","description":"An FTP server on port 2121 for copying files to and from the console.","version":"0.11","checksum":"bb","last_update":"2026-08-02","category":"Tools","author":""},
    {"id":"shsrv","name":"Shell Server","filename":"shsrv.elf","description":"A telnet shell on port 2323.","version":"0.14","checksum":"cc","last_update":"2026-07-11","category":"Tools","author":""},
    {"id":"klogsrv","name":"Kernel Log Server","filename":"klogsrv.elf","description":"Streams the kernel log over the network on port 3232.","version":"0.5","checksum":"dd","last_update":"2026-06-01","category":"Tools","author":""},
    {"id":"websrv","name":"Web Server","filename":"websrv.elf","description":"A small web server with a homebrew launcher.","version":"0.25","checksum":"ee","last_update":"2026-09-30","category":"Tools","author":""}
  ]},
  {"id":"s1","name":"Signal Suite","url":"https://marice.example/payloads.json","error":false,"payloads":[
    {"id":"ps5-homeassistant","name":"ps5-homeassistant","filename":"ps5-homeassistant.elf","description":"The PS5 as a device in Home Assistant.","version":"0.1.0","checksum":"ff","last_update":"2026-10-17","category":"Tools","author":"Marice"}
  ]}
]})";

constexpr std::string_view kSources =
    R"({"sources":[{"id":"default","name":"Official Repository","url":"https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json","removable":false},)"
    R"({"id":"s1","name":"Signal Suite","url":"https://marice.example/payloads.json","removable":true}]})";

constexpr std::string_view kProcesses = R"({"processes":[
  {"pid":64,"name":"SceShellUI","memory":610.0,"is_daemon":false},
  {"pid":101,"name":"pldmgr.elf","memory":7.4,"is_daemon":true},
  {"pid":118,"name":"package-helper.elf","memory":2.1,"is_daemon":true},
  {"pid":131,"name":"ftpsrv.elf","memory":4.2,"is_daemon":true},
  {"pid":152,"name":"ps5-signal.elf","memory":3.6,"is_daemon":true}
]})";

class DemoTransport final : public Transport
{
  public:
    Reply send(const Request &request) override
    {
        Reply reply;
        reply.status = 200;
        const std::string path = request.target.substr(0, request.target.find('?'));
        if (request.method == "POST")
        {
            if (path == "/repository_install")
                reply.body = R"({"ok":true,"slot":"repo/x@1a2b3c4d"})";
            else if (path == "/repository_refresh")
                reply.body = std::string(kRepository);
            else if (path == "/sources_add")
                reply.body = R"({"ok":true,"name":"New repository"})";
            else if (path == "/sources_remove" || path == "/process_kill")
                reply.body = R"({"ok":true})";
            else
                reply.body = "OK";
            return reply;
        }
        if (path == "/version")
            reply.body = "0.6.1";
        else if (path == "/list_payloads")
            reply.body = std::string(kPayloads);
        else if (path == "/get_config")
            reply.body = std::string(kConfig);
        else if (path == "/autoload_status")
            reply.body = std::string(kAutoload);
        else if (path == "/repository_payloads")
            reply.body = std::string(kRepository);
        else if (path == "/sources_list")
            reply.body = std::string(kSources);
        else if (path == "/processes_list")
            reply.body = std::string(kProcesses);
        else
            reply.body = "404 Not Found\n";
        return reply;
    }
};

} // namespace

std::unique_ptr<Transport> make_demo_transport()
{
    return std::make_unique<DemoTransport>();
}

} // namespace hui::payloader
