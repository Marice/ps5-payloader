// PayLoader - pldmgr's HTTP API (v0.6.x) as typed calls.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/pldmgr.hpp"

#include "payloader/json.hpp"
#include "payloader/versions.hpp"

#include <cmath>
#include <utility>

namespace hui::payloader
{

namespace
{

constexpr int kInstallTimeoutMs = 120000; // pldmgr downloads the payload before it answers
constexpr int kRefreshTimeoutMs = 60000;  // ... or every repository
constexpr int kStopTimeoutMs = 8000;      // SIGTERM, a wait, then SIGKILL

std::string text(const JsonValue &value)
{
    return value.is_string() ? value.as_string() : std::string();
}

bool flag(const JsonValue &value, bool fallback)
{
    if (value.is_bool())
        return value.as_bool();
    if (value.is_number())
        return value.as_number() != 0.0;
    return fallback;
}

int whole(const JsonValue &value, int fallback)
{
    if (!value.is_number())
        return fallback;
    const double n = value.as_number();
    if (!std::isfinite(n) || n < -2147483648.0 || n > 2147483647.0)
        return fallback;
    return static_cast<int>(n);
}

bool parse_object(std::string_view body, JsonValue &root)
{
    return parse_json(body, root) && root.is_object();
}

// The path part of a URL, as the web UI's encodeURI leaves it: "/" and "@" stay.
std::string path_encode(std::string_view text)
{
    std::string out;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t slash = text.find('/', start);
        if (slash == std::string_view::npos)
            slash = text.size();
        std::string part = url_encode(text.substr(start, slash - start));
        for (std::size_t at = part.find("%40"); at != std::string::npos; at = part.find("%40", at))
            part.replace(at, 3, "@");
        out += part;
        if (slash < text.size())
            out += '/';
        start = slash + 1;
    }
    return out;
}

std::string trim_line(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

// pldmgr answers 200 with this body for a route it does not know.
bool unknown_route(const Reply &reply)
{
    return reply.body.rfind("404 Not Found", 0) == 0;
}

bool plain_ok(const Reply &reply)
{
    return reply.status == 200 && !unknown_route(reply);
}

Outcome failure(const Reply &reply)
{
    Outcome outcome;
    outcome.error = describe_failure(reply);
    outcome.no_answer = reply.status < 0;
    return outcome;
}

Outcome from(const Reply &reply)
{
    if (!plain_ok(reply))
        return failure(reply);
    return {true, {}, false};
}

Outcome unreadable()
{
    return {false, "pldmgr answered in a way PayLoader does not understand", false};
}

} // namespace

std::string Payload::title() const
{
    if (!name.empty())
        return name;
    std::string base = filename.empty() ? path.substr(path.rfind('/') + 1) : filename;
    const std::size_t dot = base.rfind('.');
    if (dot != std::string::npos && dot > 0)
        base.erase(dot);
    return base;
}

std::string RepoPayload::key() const
{
    return !id.empty() ? id : slug(!name.empty() ? name : filename);
}

bool parse_payloads(std::string_view body, PayloadList &out)
{
    JsonValue root;
    if (!parse_object(body, root) || !root["items"].is_array())
        return false;
    PayloadList list;
    list.running_version = text(root["self"]["running_version"]);
    list.pending_version = text(root["self"]["pending_version"]);
    for (const JsonValue &item : root["items"].items())
    {
        if (!item.is_object() || text(item["slot"]).empty())
            continue;
        Payload p;
        p.slot = text(item["slot"]);
        p.path = text(item["path"]);
        p.kind = text(item["kind"]);
        p.filename = text(item["filename"]);
        p.origin = text(item["origin"]);
        p.id = text(item["id"]);
        p.name = text(item["name"]);
        p.version = text(item["version"]);
        p.checksum = text(item["checksum"]);
        p.last_update = text(item["last_update"]);
        p.author = text(item["author"]);
        p.description = text(item["description"]);
        p.source_url = text(item["source_url"]);
        p.source_name = text(item["source_name"]);
        list.items.push_back(std::move(p));
    }
    for (const JsonValue &item : root["usb"].items())
    {
        if (!item.is_string() || item.as_string().empty())
            continue;
        Payload p;
        p.kind = "usb";
        p.slot = item.as_string();
        p.path = item.as_string();
        p.filename = p.path.substr(p.path.rfind('/') + 1);
        list.items.push_back(std::move(p));
    }
    out = std::move(list);
    return true;
}

bool parse_config(std::string_view body, Config &out)
{
    JsonValue root;
    if (!parse_object(body, root))
        return false;
    Config config;
    config.autoload_enabled = flag(root["AUTOLOAD_ENABLED"], config.autoload_enabled);
    config.autoload_delay = whole(root["AUTOLOAD_DELAY"], config.autoload_delay);
    config.autoload_list = text(root["AUTOLOAD_LIST"]);
    config.auto_browser_open = flag(root["AUTO_BROWSER_OPEN"], config.auto_browser_open);
    config.kill_disc_player = flag(root["KILL_DISC_PLAYER_ON_STARTUP"], config.kill_disc_player);
    config.scan_usb = flag(root["SCAN_USB_PAYLOADS"], config.scan_usb);
    config.auto_install_app = flag(root["AUTO_INSTALL_APP"], config.auto_install_app);
    config.multi_sources = flag(root["MULTI_SOURCES_ENABLED"], config.multi_sources);
    config.favorites = text(root["FAVORITES"]);
    out = std::move(config);
    return true;
}

AutoloadState autoload_state(std::string_view name)
{
    static constexpr std::pair<std::string_view, AutoloadState> kNames[] = {
        {"disabled", AutoloadState::disabled},   {"waiting", AutoloadState::waiting},
        {"countdown", AutoloadState::countdown}, {"executing", AutoloadState::executing},
        {"done", AutoloadState::done},           {"aborted", AutoloadState::aborted},
        {"failed", AutoloadState::failed},
    };
    for (const auto &[text, state] : kNames)
        if (name == text)
            return state;
    return AutoloadState::unknown;
}

bool parse_autoload_status(std::string_view body, AutoloadStatus &out)
{
    JsonValue root;
    if (!parse_object(body, root))
        return false;
    AutoloadStatus status;
    status.state = autoload_state(text(root["state"]));
    const JsonValue &ms = root["remaining_ms"];
    status.remaining_ms = ms.is_number() && std::isfinite(ms.as_number())
                              ? static_cast<long long>(ms.as_number())
                              : 1000LL * whole(root["remaining"], 0);
    status.total = whole(root["total"], 0);
    status.done = whole(root["done"], 0);
    status.successful = whole(root["successful"], 0);
    status.failed = whole(root["failed"], 0);
    status.skipped = whole(root["skipped"], 0);
    status.current = text(root["current"]);
    status.outcomes = text(root["outcomes"]);
    out = std::move(status);
    return true;
}

bool parse_repository(std::string_view body, std::vector<RepoSource> &out)
{
    JsonValue root;
    if (!parse_object(body, root) || !root["sources"].is_array())
        return false;
    std::vector<RepoSource> sources;
    for (const JsonValue &entry : root["sources"].items())
    {
        if (!entry.is_object())
            continue;
        RepoSource source;
        source.id = text(entry["id"]);
        source.name = text(entry["name"]);
        source.url = text(entry["url"]);
        source.error = flag(entry["error"], false);
        for (const JsonValue &item : entry["payloads"].items())
        {
            if (!item.is_object())
                continue;
            RepoPayload p;
            p.id = text(item["id"]);
            p.name = text(item["name"]);
            p.filename = text(item["filename"]);
            p.description = text(item["description"]);
            p.version = text(item["version"]);
            p.checksum = text(item["checksum"]);
            p.last_update = text(item["last_update"]);
            p.category = text(item["category"]);
            p.author = text(item["author"]);
            p.release_notes = text(item["release_notes"]);
            if (p.name.empty() && p.filename.empty() && p.id.empty())
                continue;
            source.payloads.push_back(std::move(p));
        }
        sources.push_back(std::move(source));
    }
    out = std::move(sources);
    return true;
}

bool parse_sources(std::string_view body, std::vector<Source> &out)
{
    JsonValue root;
    if (!parse_object(body, root) || !root["sources"].is_array())
        return false;
    std::vector<Source> sources;
    for (const JsonValue &entry : root["sources"].items())
    {
        if (!entry.is_object())
            continue;
        Source source;
        source.id = text(entry["id"]);
        source.name = text(entry["name"]);
        source.url = text(entry["url"]);
        source.removable = flag(entry["removable"], true);
        sources.push_back(std::move(source));
    }
    out = std::move(sources);
    return true;
}

bool parse_processes(std::string_view body, std::vector<Process> &out)
{
    JsonValue root;
    if (!parse_object(body, root) || !root["processes"].is_array())
        return false;
    std::vector<Process> processes;
    for (const JsonValue &entry : root["processes"].items())
    {
        const int pid = whole(entry["pid"], -1);
        if (!entry.is_object() || pid <= 0)
            continue;
        Process process;
        process.pid = pid;
        process.name = text(entry["name"]);
        process.memory_mib = entry["memory"].as_number(0.0);
        process.payload = flag(entry["is_daemon"], false);
        processes.push_back(std::move(process));
    }
    out = std::move(processes);
    return true;
}

bool parse_install(std::string_view body, InstallResult &out)
{
    JsonValue root;
    if (!parse_object(body, root))
        return false;
    InstallResult result;
    result.ok = flag(root["ok"], false);
    result.slot = text(root["slot"]);
    result.version = text(root["version"]);
    result.self_update = flag(root["self_update"], false);
    result.message = text(root["message"]);
    const JsonValue &conflict = root["conflict"];
    if (conflict.is_object())
    {
        result.conflict = true;
        result.conflict_slot = text(conflict["slot"]);
        result.conflict_source = text(conflict["source_name"]);
        result.conflict_version = text(conflict["version"]);
    }
    out = std::move(result);
    return true;
}

std::string config_body(const ConfigChange &change)
{
    std::string body = "{";
    const auto key = [&body](const char *name)
    {
        if (body.size() > 1)
            body += ',';
        append_json_string(body, name);
        body += ':';
    };
    const auto boolean = [&](const char *name, const std::optional<bool> &value)
    {
        if (value)
        {
            key(name);
            body += *value ? "true" : "false";
        }
    };
    const auto string = [&](const char *name, const std::optional<std::string> &value)
    {
        if (value)
        {
            key(name);
            append_json_string(body, *value);
        }
    };
    boolean("AUTOLOAD_ENABLED", change.autoload_enabled);
    if (change.autoload_delay)
    {
        key("AUTOLOAD_DELAY");
        body += std::to_string(*change.autoload_delay);
    }
    string("AUTOLOAD_LIST", change.autoload_list);
    boolean("AUTO_BROWSER_OPEN", change.auto_browser_open);
    boolean("KILL_DISC_PLAYER_ON_STARTUP", change.kill_disc_player);
    boolean("SCAN_USB_PAYLOADS", change.scan_usb);
    boolean("AUTO_INSTALL_APP", change.auto_install_app);
    boolean("MULTI_SOURCES_ENABLED", change.multi_sources);
    string("FAVORITES", change.favorites);
    return body + "}";
}

std::string describe_failure(const Reply &reply)
{
    if (reply.status < 0)
        return "pldmgr does not answer on port 8084";
    if (unknown_route(reply))
        return "This pldmgr does not know this request; it may be older or newer than PayLoader";
    if (reply.status == 403)
        return "pldmgr refused the change";
    JsonValue root;
    if (parse_object(reply.body, root))
    {
        for (const char *name : {"error", "message"})
            if (!text(root[name]).empty())
                return text(root[name]);
    }
    const std::string line = trim_line(reply.body);
    if (!line.empty() && line.size() <= 160 && line.find('\n') == std::string::npos &&
        line.front() != '{' && line.front() != '<')
        return line;
    return "pldmgr answered with status " + std::to_string(reply.status);
}

// ---- Pldmgr -------------------------------------------------------------------

Pldmgr::Pldmgr(std::unique_ptr<Transport> transport) : transport_(std::move(transport))
{
}

Reply Pldmgr::get(std::string target, int timeout_ms)
{
    Request request;
    request.method = "GET";
    request.target = std::move(target);
    request.timeout_ms = timeout_ms;
    return transport_->send(request);
}

Reply Pldmgr::post(std::string target, std::string body, int timeout_ms)
{
    Request request;
    request.method = "POST";
    request.target = std::move(target);
    request.headers.emplace_back("X-PLDMGR-Request", "1");
    if (!body.empty())
        request.headers.emplace_back("Content-Type", "application/json");
    request.body = std::move(body);
    request.timeout_ms = timeout_ms;
    return transport_->send(request);
}

Outcome Pldmgr::version(std::string &version, bool &supported)
{
    const Reply reply = get("/version");
    Outcome outcome = from(reply);
    if (!outcome.ok)
        return outcome;
    version = trim_line(reply.body);
    supported = version_at_least(version, kMinimumMajor, kMinimumMinor, kMinimumPatch);
    return outcome;
}

Outcome Pldmgr::payloads(PayloadList &out)
{
    const Reply reply = get("/list_payloads");
    Outcome outcome = from(reply);
    if (outcome.ok && !parse_payloads(reply.body, out))
        return unreadable();
    return outcome;
}

Outcome Pldmgr::launch(std::string_view slot_or_path)
{
    return from(post("/loadpayload:" + path_encode(slot_or_path)));
}

Outcome Pldmgr::remove(std::string_view slot)
{
    return from(post("/manage:delete?slot=" + url_encode(slot)));
}

Outcome Pldmgr::config(Config &out)
{
    const Reply reply = get("/get_config");
    Outcome outcome = from(reply);
    if (outcome.ok && !parse_config(reply.body, out))
        return unreadable();
    return outcome;
}

Outcome Pldmgr::save_config(const ConfigChange &change)
{
    return from(post("/set_config", config_body(change)));
}

Outcome Pldmgr::autoload_status(AutoloadStatus &out)
{
    const Reply reply = get("/autoload_status");
    Outcome outcome = from(reply);
    if (outcome.ok && !parse_autoload_status(reply.body, out))
        return unreadable();
    return outcome;
}

Outcome Pldmgr::abort_autoload()
{
    return from(post("/abort"));
}

Outcome Pldmgr::repository(bool refresh, std::vector<RepoSource> &out)
{
    const Reply reply = refresh ? post("/repository_refresh", {}, kRefreshTimeoutMs)
                                : get("/repository_payloads", kRefreshTimeoutMs);
    Outcome outcome = from(reply);
    if (outcome.ok && !parse_repository(reply.body, out))
        return unreadable();
    return outcome;
}

Outcome Pldmgr::install(const RepoPayload &payload, std::string_view source_url, InstallMode mode,
                        std::string_view replace_slot, InstallResult &out)
{
    std::string target = "/repository_install?id=" + url_encode(payload.key());
    if (!source_url.empty())
        target += "&source_url=" + url_encode(source_url);
    if (mode == InstallMode::replace)
        target += "&mode=replace";
    else if (mode == InstallMode::keep_both)
        target += "&mode=keep_both";
    if (!replace_slot.empty())
        target += "&replace_slot=" + url_encode(replace_slot);
    const Reply reply = post(std::move(target), {}, kInstallTimeoutMs);
    if (reply.status < 0 || unknown_route(reply))
        return from(reply);
    // A conflict or a refusal comes back as JSON with "ok": false.
    if (!parse_install(reply.body, out))
        return reply.status == 200 ? unreadable() : from(reply);
    if (out.ok || out.conflict)
        return {true, {}, false};
    return {false, !out.message.empty() ? out.message : describe_failure(reply), false};
}

Outcome Pldmgr::sources(std::vector<Source> &out)
{
    const Reply reply = get("/sources_list");
    Outcome outcome = from(reply);
    if (outcome.ok && !parse_sources(reply.body, out))
        return unreadable();
    return outcome;
}

Outcome Pldmgr::add_source(std::string_view url, std::string &name)
{
    const Reply reply = post("/sources_add?url=" + url_encode(url), {}, kRefreshTimeoutMs);
    JsonValue root;
    if (reply.status == 200 && parse_object(reply.body, root) && flag(root["ok"], false))
    {
        name = text(root["name"]);
        return {true, {}, false};
    }
    return failure(reply);
}

Outcome Pldmgr::remove_source(int index)
{
    const Reply reply = post("/sources_remove?index=" + std::to_string(index));
    JsonValue root;
    if (reply.status == 200 && parse_object(reply.body, root) && flag(root["ok"], false))
        return {true, {}, false};
    return failure(reply);
}

Outcome Pldmgr::processes(std::vector<Process> &out)
{
    const Reply reply = get("/processes_list");
    Outcome outcome = from(reply);
    if (outcome.ok && !parse_processes(reply.body, out))
        return unreadable();
    return outcome;
}

Outcome Pldmgr::stop(int pid)
{
    const Reply reply = post("/process_kill?pid=" + std::to_string(pid), {}, kStopTimeoutMs);
    JsonValue root;
    if (reply.status == 200 && parse_object(reply.body, root) && flag(root["ok"], false))
        return {true, {}, false};
    return failure(reply);
}

} // namespace hui::payloader
