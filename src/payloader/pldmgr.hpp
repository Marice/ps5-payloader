// PayLoader - pldmgr's HTTP API (v0.6.x) as typed calls.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pldmgr (itsPLK/ps5-payload-manager) listens on port 8084. Reads are GET;
// changes are POST with the header "X-PLDMGR-Request: 1", which is how a
// client that is not a browser passes its origin check. The API has no
// version of its own, so PayLoader checks pldmgr's version (0.6.0 or newer:
// 0.6.0 moved every change from GET to POST) and reads every field with a
// fallback.
//
// The parse_* functions take a reply body and are what the tests cover;
// Pldmgr wraps them around a Transport. Every call blocks: run them on a
// JobWorker, never in a frame.
//
//   Pldmgr api(make_loopback_transport(kPldmgrPort));
//   PayloadList list;
//   const Outcome got = api.payloads(list);
//   if (!got.ok) show(got.error);

#pragma once

#include "payloader/http.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hui::payloader
{

constexpr int kPldmgrPort = 8084;

// The oldest pldmgr whose API PayLoader speaks.
constexpr int kMinimumMajor = 0;
constexpr int kMinimumMinor = 6;
constexpr int kMinimumPatch = 0;

struct Outcome
{
    bool ok = false;
    std::string error;      // one sentence for a toast; "" when ok
    bool no_answer = false; // nothing answered: pldmgr is gone
};

// An installed payload: kind "local" (added by hand) or "repo" (installed
// from a repository), or a payload pldmgr found on a USB drive ("usb").
struct Payload
{
    std::string slot; // "local/ftpsrv", "repo/kstuff@1a2b3c4d"; the path for usb
    std::string path;
    std::string kind;
    std::string filename;
    // local
    std::string origin; // "web_upload", "usb_move", ...
    // repo
    std::string id;
    std::string name;
    std::string version;
    std::string checksum;
    std::string last_update;
    std::string author;
    std::string description;
    std::string source_url;
    std::string source_name;

    // The name to show: the repository's name, else the file name without ".elf".
    std::string title() const;
};

struct PayloadList
{
    std::string running_version; // pldmgr's own
    std::string pending_version; // a downloaded self-update, "" when none
    std::vector<Payload> items;  // installed, then USB
};

struct RepoPayload
{
    std::string id;
    std::string name;
    std::string filename;
    std::string description;
    std::string version;
    std::string checksum;
    std::string last_update;
    std::string category;
    std::string author;
    std::string release_notes;

    std::string key() const; // id, or the slug of the name pldmgr would use
};

struct RepoSource
{
    std::string id;
    std::string name;
    std::string url;
    bool error = false; // the last refresh of this source failed
    std::vector<RepoPayload> payloads;
};

struct Source
{
    std::string id;
    std::string name;
    std::string url;
    bool removable = true;
};

struct Process
{
    int pid = 0;
    std::string name;
    double memory_mib = 0.0;
    bool payload = false; // pldmgr's "is_daemon": an ELF without an app
};

struct Config
{
    bool autoload_enabled = false;
    int autoload_delay = 5;    // seconds before the list starts
    std::string autoload_list; // "local/a,!2000,repo/b@h"
    bool auto_browser_open = true;
    bool kill_disc_player = true;
    bool scan_usb = false;
    bool auto_install_app = true;
    bool multi_sources = false; // without it pldmgr reads only its default repository
    std::string favorites;      // slots, comma separated
};

// The fields to change in one /set_config; the rest stay as they are.
struct ConfigChange
{
    std::optional<bool> autoload_enabled;
    std::optional<int> autoload_delay;
    std::optional<std::string> autoload_list;
    std::optional<bool> auto_browser_open;
    std::optional<bool> kill_disc_player;
    std::optional<bool> scan_usb;
    std::optional<bool> auto_install_app;
    std::optional<bool> multi_sources;
    std::optional<std::string> favorites;
};

enum class AutoloadState : unsigned char
{
    unknown,
    disabled,
    waiting,
    countdown,
    executing,
    done,
    aborted,
    failed,
};

struct AutoloadStatus
{
    AutoloadState state = AutoloadState::unknown;
    long long remaining_ms = 0; // of the countdown
    int total = 0;
    int done = 0;
    int successful = 0;
    int failed = 0;
    int skipped = 0;
    std::string current;  // the entry being loaded
    std::string outcomes; // one letter per entry: p pending, s success, f failed, k skipped
};

enum class InstallMode : unsigned char
{
    normal,
    replace, // over replace_slot
    keep_both,
};

struct InstallResult
{
    bool ok = false;
    std::string slot;
    std::string version;
    bool self_update = false; // pldmgr updated itself; it applies at its next start
    // Set when the same payload is installed from another repository.
    bool conflict = false;
    std::string conflict_slot;
    std::string conflict_source;
    std::string conflict_version;
    std::string message;
};

// ---- Parsing (pure) ---------------------------------------------------------

bool parse_payloads(std::string_view body, PayloadList &out);
bool parse_config(std::string_view body, Config &out);
bool parse_autoload_status(std::string_view body, AutoloadStatus &out);
bool parse_repository(std::string_view body, std::vector<RepoSource> &out);
bool parse_sources(std::string_view body, std::vector<Source> &out);
bool parse_processes(std::string_view body, std::vector<Process> &out);
bool parse_install(std::string_view body, InstallResult &out);
AutoloadState autoload_state(std::string_view name);
std::string config_body(const ConfigChange &change);

// What went wrong, in one sentence, for a reply that is not a success.
std::string describe_failure(const Reply &reply);

// ---- Calls ------------------------------------------------------------------

class Pldmgr
{
  public:
    explicit Pldmgr(std::unique_ptr<Transport> transport);

    // pldmgr's version, and whether PayLoader speaks its API.
    Outcome version(std::string &version, bool &supported);
    Outcome payloads(PayloadList &out);
    Outcome launch(std::string_view slot_or_path);
    Outcome remove(std::string_view slot);
    Outcome config(Config &out);
    Outcome save_config(const ConfigChange &change);
    Outcome autoload_status(AutoloadStatus &out);
    Outcome abort_autoload();
    // refresh: fetch every source again instead of using pldmgr's cache.
    Outcome repository(bool refresh, std::vector<RepoSource> &out);
    Outcome install(const RepoPayload &payload, std::string_view source_url, InstallMode mode,
                    std::string_view replace_slot, InstallResult &out);
    Outcome sources(std::vector<Source> &out);
    Outcome add_source(std::string_view url, std::string &name);
    Outcome remove_source(int index);
    Outcome processes(std::vector<Process> &out);
    Outcome stop(int pid);

  private:
    Reply get(std::string target, int timeout_ms = 3000);
    Reply post(std::string target, std::string body = {}, int timeout_ms = 5000);

    std::unique_ptr<Transport> transport_;
};

} // namespace hui::payloader
