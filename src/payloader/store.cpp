// PayLoader - What pldmgr holds, kept fresh in the background for the screens.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/store.hpp"

#include "payloader/repo_status.hpp"
#include "payloader/suite.hpp"
#include "payloader/versions.hpp"

#include <algorithm>
#include <utility>

namespace hui::payloader
{

namespace
{

constexpr float kWantedFor = 1.0f; // a want() keeps a topic fresh this long
constexpr float kLinkEvery = 3.0f; // seconds between checks while pldmgr is away
constexpr float kEvery[] = {
    4.0f, // payloads (and the config with them: favourites, the autoload list)
    3.0f, // processes
    1.0f, // autoload progress
    0.0f, // repository: once, then on request
    0.0f, // sources: once, then after a change
};
static_assert(std::size(kEvery) == static_cast<std::size_t>(Topic::count));
constexpr std::size_t kMaxWaiting = 3;
constexpr float kChainEvery = 3.0f; // seconds between reads of the boot chain

std::size_t index(Topic topic)
{
    return static_cast<std::size_t>(topic);
}

std::vector<std::string> split_csv(const std::string &text)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t comma = text.find(',', start);
        if (comma == std::string::npos)
            comma = text.size();
        if (comma > start)
            out.push_back(text.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

std::string join_csv(const std::vector<std::string> &items)
{
    std::string out;
    for (const std::string &item : items)
    {
        if (!out.empty())
            out += ',';
        out += item;
    }
    return out;
}

} // namespace

Store::Store(TransportFactory factory, std::string root)
    : factory_(std::move(factory)), root_(std::move(root))
{
}

void Store::want(Topic topic)
{
    wanted_[index(topic)] = kWantedFor;
}

void Store::update(float dt)
{
    poll(dt);
    std::lock_guard<std::mutex> lock(mutex_);
    if (shown_revision_ != revision_)
    {
        shown_ = state_;
        shown_revision_ = revision_;
    }
}

std::vector<Notice> Store::take_notices()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(notices_, {});
}

void Store::refresh()
{
    since_link_ = 1000.0f;
    since_chain_ = 1000.0f;
    since_.fill(1000.0f);
    repository_asked_ = false;
    sources_asked_ = false;
}

void Store::poll(float dt)
{
    if (relinked_.exchange(false))
        since_.fill(1000.0f);
    since_link_ += dt;
    for (std::size_t i = 0; i < wanted_.size(); ++i)
    {
        wanted_[i] = std::max(0.0f, wanted_[i] - dt);
        since_[i] += dt;
    }
    if (worker_.waiting() >= kMaxWaiting)
        return; // still busy with earlier ones

    // The boot chain comes from files on the console, so it is read with or without pldmgr.
    since_chain_ += dt;
    const bool chain_wanted =
        wanted_[index(Topic::autoload)] > 0.0f || wanted_[index(Topic::payloads)] > 0.0f;
    if (chain_wanted && since_chain_ >= kChainEvery)
    {
        since_chain_ = 0.0f;
        worker_.queue([this] { read_chain(); });
    }

    if (!linked())
    {
        if (since_link_ >= kLinkEvery)
        {
            since_link_ = 0.0f;
            run([this](Pldmgr &api) { check_link(api); });
        }
        return;
    }

    const auto due = [&](Topic topic)
    {
        const std::size_t i = index(topic);
        if (wanted_[i] <= 0.0f || since_[i] < kEvery[i])
            return false;
        since_[i] = 0.0f;
        return true;
    };
    if (due(Topic::payloads))
        run(
            [this](Pldmgr &api)
            {
                read_payloads(api);
                read_config(api);
            });
    if (due(Topic::processes))
        run(
            [this](Pldmgr &api)
            {
                std::vector<Process> processes;
                const Outcome got = api.processes(processes);
                std::lock_guard<std::mutex> lock(mutex_);
                if (got.no_answer)
                    state_.link = Link::offline;
                if (got.ok)
                {
                    state_.processes = std::move(processes);
                    state_.have_processes = true;
                }
                ++revision_;
            });
    if (due(Topic::autoload))
        run(
            [this](Pldmgr &api)
            {
                AutoloadStatus status;
                const Outcome got = api.autoload_status(status);
                std::lock_guard<std::mutex> lock(mutex_);
                if (got.no_answer)
                    state_.link = Link::offline;
                if (got.ok)
                {
                    state_.autoload = std::move(status);
                    state_.have_autoload = true;
                }
                ++revision_;
            });
    if (wanted_[index(Topic::autoload)] > 0.0f && since_[index(Topic::payloads)] >= 4.0f)
    {
        // The Autoload screen edits the list in the config; keep it and the payloads known.
        since_[index(Topic::payloads)] = 0.0f;
        run(
            [this](Pldmgr &api)
            {
                read_payloads(api);
                read_config(api);
            });
    }
    if (wanted_[index(Topic::repository)] > 0.0f && !repository_asked_)
    {
        repository_asked_ = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.repository_busy = true;
            ++revision_;
        }
        run(
            [this](Pldmgr &api)
            {
                std::vector<RepoSource> sources;
                const Outcome got = api.repository(false, sources);
                read_payloads(api);
                std::lock_guard<std::mutex> lock(mutex_);
                state_.repository_busy = false;
                state_.repository_error = got.ok ? std::string() : got.error;
                if (got.ok)
                {
                    state_.repository = std::move(sources);
                    state_.have_repository = true;
                }
                ++revision_;
            });
    }
    if (wanted_[index(Topic::sources)] > 0.0f && !sources_asked_)
    {
        sources_asked_ = true;
        run(
            [this](Pldmgr &api)
            {
                std::vector<Source> sources;
                const Outcome got = api.sources(sources);
                read_config(api);
                std::lock_guard<std::mutex> lock(mutex_);
                if (got.ok)
                {
                    state_.sources = std::move(sources);
                    state_.have_sources = true;
                }
                ++revision_;
            });
    }
}

bool Store::linked()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.link == Link::ready;
}

void Store::run(std::function<void(Pldmgr &)> job)
{
    worker_.queue(
        [this, job = std::move(job)]
        {
            if (!api_)
                api_ = std::make_unique<Pldmgr>(factory_());
            job(*api_);
        });
}

void Store::check_link(Pldmgr &api)
{
    std::string version;
    bool supported = false;
    const Outcome got = api.version(version, supported);
    Link link = Link::ready;
    if (got.no_answer)
        link = Link::offline;
    else if (!got.ok || !version_at_least(version, 0, 0, 0))
        link = Link::unknown;
    else if (!supported)
        link = Link::too_old;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool came_back = state_.link != Link::ready && link == Link::ready;
        state_.link = link;
        state_.version = link == Link::offline ? std::string() : version;
        ++revision_;
        if (!came_back)
            return;
    }
    // Fresh after (re)connecting: everything a screen may want.
    relinked_ = true;
    repository_asked_ = false;
    sources_asked_ = false;
    ensure_suite_source(api);
    read_payloads(api);
    read_config(api);
}

void Store::read_payloads(Pldmgr &api)
{
    PayloadList list;
    const Outcome got = api.payloads(list);
    std::lock_guard<std::mutex> lock(mutex_);
    if (got.no_answer)
        state_.link = Link::offline;
    if (got.ok)
    {
        state_.payloads = std::move(list);
        state_.have_payloads = true;
    }
    ++revision_;
}

void Store::read_config(Pldmgr &api)
{
    Config config;
    const Outcome got = api.config(config);
    std::lock_guard<std::mutex> lock(mutex_);
    if (got.no_answer)
        state_.link = Link::offline;
    if (got.ok)
    {
        state_.config = std::move(config);
        state_.have_config = true;
    }
    ++revision_;
}

void Store::notice(Notice notice)
{
    std::lock_guard<std::mutex> lock(mutex_);
    notices_.push_back(std::move(notice));
}

void Store::note(const Outcome &outcome, const std::string &title)
{
    Notice n;
    n.failed = !outcome.ok;
    n.title = title;
    n.detail = outcome.error;
    if (outcome.no_answer)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.link = Link::offline;
        ++revision_;
    }
    notice(std::move(n));
}

void Store::mark_busy(const std::string &key, bool busy)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy)
        state_.busy.insert(key);
    else
        state_.busy.erase(key);
    ++revision_;
}

void Store::launch(const Payload &payload)
{
    const std::string key = payload.slot;
    const std::string title = payload.title();
    mark_busy(key, true);
    run(
        [this, key, title](Pldmgr &api)
        {
            const Outcome done = api.launch(key);
            note(done, done.ok ? title + " started" : title + " did not start");
            mark_busy(key, false);
        });
}

void Store::remove(const Payload &payload)
{
    const std::string key = payload.slot;
    const std::string title = payload.title();
    mark_busy(key, true);
    run(
        [this, key, title](Pldmgr &api)
        {
            const Outcome done = api.remove(key);
            note(done, done.ok ? title + " deleted" : title + " was not deleted");
            mark_busy(key, false);
            read_payloads(api);
            read_config(api);
        });
}

void Store::set_favourite(const std::string &slot, bool favourite)
{
    mark_busy(slot, true);
    run(
        [this, slot, favourite](Pldmgr &api)
        {
            // From pldmgr's current list, so a change made in the browser meanwhile stays.
            Config config;
            Outcome done = api.config(config);
            if (done.ok)
            {
                std::vector<std::string> list = split_csv(config.favorites);
                list.erase(std::remove(list.begin(), list.end(), slot), list.end());
                if (favourite)
                    list.push_back(slot);
                ConfigChange change;
                change.favorites = join_csv(list);
                done = api.save_config(change);
            }
            if (!done.ok)
                note(done, "Favourites not saved");
            mark_busy(slot, false);
            read_config(api);
        });
}

void Store::save_autoload(bool enabled, int delay_seconds, const std::string &list)
{
    ConfigChange change;
    change.autoload_enabled = enabled;
    change.autoload_delay = std::clamp(delay_seconds, 0, 3600);
    change.autoload_list = list;
    run(
        [this, change](Pldmgr &api)
        {
            const Outcome done = api.save_config(change);
            note(done, done.ok ? "Autoload saved" : "Autoload not saved");
            read_config(api);
        });
}

void Store::abort_autoload()
{
    run(
        [this](Pldmgr &api)
        {
            const Outcome done = api.abort_autoload();
            note(done, done.ok ? "Autoload stopped" : "Autoload did not stop");
        });
}

void Store::refresh_repository()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.repository_busy)
            return;
        state_.repository_busy = true;
        ++revision_;
    }
    repository_asked_ = true;
    run(
        [this](Pldmgr &api)
        {
            std::vector<RepoSource> sources;
            const Outcome got = api.repository(true, sources);
            if (!got.ok)
                note(got, "The repositories were not refreshed");
            read_payloads(api);
            std::lock_guard<std::mutex> lock(mutex_);
            state_.repository_busy = false;
            state_.repository_error = got.ok ? std::string() : got.error;
            if (got.ok)
            {
                state_.repository = std::move(sources);
                state_.have_repository = true;
            }
            ++revision_;
        });
}

void Store::install(const RepoPayload &payload, const std::string &source_url, InstallMode mode,
                    const std::string &replace_slot)
{
    const std::string key = "install:" + source_url + "#" + payload.key();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.busy.count(key) != 0)
            return;
    }
    mark_busy(key, true);
    run(
        [this, key, payload, source_url, mode, replace_slot](Pldmgr &api)
        {
            InstallResult result;
            const Outcome done = api.install(payload, source_url, mode, replace_slot, result);
            const std::string title = !payload.name.empty() ? payload.name : payload.filename;
            if (done.ok && result.conflict)
            {
                Notice n;
                n.conflict = true;
                n.title = title + " is installed from another repository";
                n.payload = payload;
                n.source_url = source_url;
                n.conflict_slot = result.conflict_slot;
                n.conflict_source = result.conflict_source;
                n.conflict_version = result.conflict_version;
                notice(std::move(n));
            }
            else if (done.ok && result.self_update)
            {
                Notice n;
                n.title = "pldmgr " + result.version + " downloaded";
                n.detail = "It takes over the next time pldmgr starts";
                notice(std::move(n));
            }
            else
            {
                note(done, done.ok ? title + " installed" : title + " was not installed");
            }
            mark_busy(key, false);
            read_payloads(api);
        });
}

void Store::add_source(const std::string &url)
{
    mark_busy("source:add", true);
    run(
        [this, url](Pldmgr &api)
        {
            std::string name;
            const Outcome done = api.add_source(url, name);
            note(done, done.ok ? "Added " + (name.empty() ? url : name) : "Repository not added");
            mark_busy("source:add", false);
            sources_asked_ = false;
            repository_asked_ = false;
        });
}

void Store::remove_source(int index, const std::string &name)
{
    mark_busy("source:" + std::to_string(index), true);
    run(
        [this, index, name](Pldmgr &api)
        {
            const Outcome done = api.remove_source(index);
            note(done, done.ok ? "Removed " + name : name + " was not removed");
            mark_busy("source:" + std::to_string(index), false);
            sources_asked_ = false;
            repository_asked_ = false;
        });
}

void Store::stop(const Process &process)
{
    const std::string key = "pid:" + std::to_string(process.pid);
    const std::string name = process.name;
    const int pid = process.pid;
    mark_busy(key, true);
    run(
        [this, key, name, pid](Pldmgr &api)
        {
            const Outcome done = api.stop(pid);
            note(done, done.ok ? name + " stopped" : name + " did not stop");
            mark_busy(key, false);
            std::vector<Process> processes;
            if (api.processes(processes).ok)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_.processes = std::move(processes);
                ++revision_;
            }
        });
}

void Store::save_settings(const ConfigChange &change)
{
    run(
        [this, change](Pldmgr &api)
        {
            const Outcome done = api.save_config(change);
            note(done, done.ok ? "Saved" : "Not saved");
            read_config(api);
        });
}

void Store::read_chain()
{
    Chain chain = detect_chain(root_);
    AutoloaderList list;
    if (chain.kind == ChainKind::autoloader && !read_autoloader(root_, chain.directory, list))
        chain.kind = ChainKind::unknown;
    std::lock_guard<std::mutex> lock(mutex_);
    state_.chain = std::move(chain);
    state_.chain_list = std::move(list);
    state_.have_chain = true;
    ++revision_;
}

void Store::save_chain(const std::vector<AutoloadEntry> &entries)
{
    worker_.queue(
        [this, entries]
        {
            Chain chain = detect_chain(root_);
            std::string error = chain.kind == ChainKind::autoloader
                                    ? write_autoloader(root_, chain.directory, entries, today())
                                    : "ps5-autoloader's list is not found";
            Notice n;
            n.failed = !error.empty();
            n.title = error.empty() ? "Boot list saved" : "Boot list not saved";
            n.detail = error.empty() ? chain.directory + "/autoload.txt" : error;
            notice(std::move(n));
            read_chain();
        });
}

void Store::add_to_chain(const Payload &payload)
{
    const std::string source = payload.path;
    const std::string name = payload.filename.empty()
                                 ? payload.path.substr(payload.path.rfind('/') + 1)
                                 : payload.filename;
    worker_.queue(
        [this, source, name]
        {
            Chain chain = detect_chain(root_);
            AutoloaderList list;
            std::string error;
            if (chain.kind != ChainKind::autoloader ||
                !read_autoloader(root_, chain.directory, list))
                error = "ps5-autoloader's list is not found";
            if (error.empty())
                error = copy_into_chain(root_, chain.directory, source, name);
            if (error.empty())
            {
                if (!list.entries.empty() && !list.entries.back().wait)
                    list.entries.push_back(AutoloadEntry::pause(3000));
                list.entries.push_back(AutoloadEntry::payload(name));
                error = write_autoloader(root_, chain.directory, list.entries, today());
            }
            Notice n;
            n.failed = !error.empty();
            n.title = error.empty() ? name + " starts with the console" : name + " not added";
            n.detail = error;
            notice(std::move(n));
            read_chain();
        });
}

void Store::remove_from_chain(const std::string &file)
{
    worker_.queue(
        [this, file]
        {
            Chain chain = detect_chain(root_);
            AutoloaderList list;
            std::string error;
            if (chain.kind != ChainKind::autoloader ||
                !read_autoloader(root_, chain.directory, list))
                error = "ps5-autoloader's list is not found";
            if (error.empty())
            {
                // The entry, and the wait right before it, so no double wait is left behind.
                std::vector<AutoloadEntry> kept;
                for (const AutoloadEntry &entry : list.entries)
                {
                    if (!entry.wait && entry.target == file)
                    {
                        if (!kept.empty() && kept.back().wait)
                            kept.pop_back();
                        continue;
                    }
                    kept.push_back(entry);
                }
                error = write_autoloader(root_, chain.directory, kept, today());
            }
            Notice n;
            n.failed = !error.empty();
            n.title =
                error.empty() ? file + " no longer starts with the console" : file + " not removed";
            n.detail = error;
            notice(std::move(n));
            read_chain();
        });
}

// The suite's repository is always among pldmgr's sources, and pldmgr shows extra sources.
void Store::ensure_suite_source(Pldmgr &api)
{
    std::vector<Source> sources;
    Config config;
    if (!api.sources(sources).ok || !api.config(config).ok)
        return;
    bool listed = false;
    for (const Source &source : sources)
        listed = listed || is_suite_url(source.url);
    bool changed = false;
    if (!listed)
    {
        std::string name;
        changed = api.add_source(kSuiteUrl, name).ok;
    }
    if (!config.multi_sources)
    {
        ConfigChange change;
        change.multi_sources = true;
        changed = api.save_config(change).ok || changed;
    }
    if (!changed)
        return;
    repository_asked_ = false;
    sources_asked_ = false;
    if (suite_told_)
        return;
    suite_told_ = true;
    Notice n;
    n.title = "Signal suite added to pldmgr";
    n.detail = "pldmgr now also shows its extra repositories";
    notice(std::move(n));
}

namespace
{

// The suite's payload with this id in its repository, or null.
const RepoPayload *suite_payload(const RepoSource &suite, const char *id)
{
    for (const RepoPayload &p : suite.payloads)
        if (p.key() == id)
            return &p;
    return nullptr;
}

// The installed copy of a suite payload: from the suite's repository first, else any repository.
const Payload *installed_suite_payload(const PayloadList &installed, const char *id)
{
    const Payload *any = nullptr;
    for (const Payload &p : installed.items)
    {
        if (p.kind != "repo" || p.id != id)
            continue;
        if (is_suite_url(p.source_url))
            return &p;
        if (any == nullptr)
            any = &p;
    }
    return any;
}

} // namespace

void Store::install_suite()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.busy.count("suite") != 0)
            return;
    }
    mark_busy("suite", true);
    run(
        [this](Pldmgr &api)
        {
            std::vector<RepoSource> sources;
            const Outcome got = api.repository(false, sources);
            const RepoSource *suite = got.ok ? suite_source(sources) : nullptr;
            PayloadList installed;
            const Outcome have = api.payloads(installed);
            bool complete = suite != nullptr && have.ok;
            int changed = 0;
            if (!complete)
            {
                Notice n;
                n.failed = true;
                n.title = "The Signal suite is not available";
                n.detail = !got.ok ? got.error : "pldmgr has not read its repository yet";
                notice(std::move(n));
            }
            for (std::size_t i = 0; complete && i < kSuiteIds.size(); ++i)
            {
                const RepoPayload *payload = suite_payload(*suite, kSuiteIds[i]);
                if (payload == nullptr)
                {
                    complete = false;
                    Notice n;
                    n.failed = true;
                    n.title = std::string(kSuiteIds[i]) + " is missing from the suite";
                    notice(std::move(n));
                    break;
                }
                if (repo_status(*payload, suite->url, installed).kind ==
                    RepoStatus::Kind::installed)
                    continue;
                InstallResult result;
                const Outcome done =
                    api.install(*payload, suite->url, InstallMode::normal, "", result);
                if (done.ok && result.conflict)
                {
                    // pldmgr's own question: replace the copy from another repository? The rest
                    // waits.
                    Notice n;
                    n.conflict = true;
                    n.title = payload->name + " is installed from another repository";
                    n.payload = *payload;
                    n.source_url = suite->url;
                    n.conflict_slot = result.conflict_slot;
                    n.conflict_source = result.conflict_source;
                    n.conflict_version = result.conflict_version;
                    notice(std::move(n));
                    complete = false;
                    break;
                }
                note(done, done.ok ? payload->name + " " + payload->version + " installed"
                                   : payload->name + " was not installed");
                if (!done.ok)
                {
                    complete = false;
                    break;
                }
                ++changed;
            }
            read_payloads(api);
            if (complete)
            {
                Notice n;
                n.suite_installed = true;
                n.title = changed > 0 ? "The Signal suite is installed"
                                      : "The Signal suite was up to date";
                notice(std::move(n));
            }
            mark_busy("suite", false);
        });
}

void Store::add_suite_to_chain()
{
    run(
        [this](Pldmgr &api)
        {
            PayloadList installed;
            std::string error;
            if (!api.payloads(installed).ok)
                error = "pldmgr does not answer";
            const Chain chain = detect_chain(root_);
            if (error.empty() && chain.kind == ChainKind::autoloader)
            {
                AutoloaderList list;
                if (!read_autoloader(root_, chain.directory, list))
                    error = "ps5-autoloader's list cannot be read";
                std::vector<AutoloadEntry> entries = list.entries;
                for (const char *id : kSuiteIds)
                {
                    const Payload *p = installed_suite_payload(installed, id);
                    if (!error.empty() || p == nullptr)
                        continue;
                    // A newer build replaces an older copy of the same payload in the folder.
                    error = copy_into_chain(root_, chain.directory, p->path, p->filename, true);
                    bool listed = false;
                    for (const AutoloadEntry &e : entries)
                        listed = listed || (!e.wait && suite_rank(e.target) == suite_rank(id));
                    if (error.empty() && !listed)
                        entries = with_suite_entry(entries, p->filename);
                }
                if (error.empty())
                    error = write_autoloader(root_, chain.directory, entries, today());
            }
            else if (error.empty())
            {
                Config config;
                if (!api.config(config).ok)
                    error = "pldmgr's settings cannot be read";
                std::vector<AutoloadEntry> entries = parse_autoload_list(config.autoload_list);
                for (const char *id : kSuiteIds)
                {
                    const Payload *p = installed_suite_payload(installed, id);
                    bool listed = false;
                    for (const AutoloadEntry &e : entries)
                        listed = listed || (!e.wait && suite_rank(e.target) == suite_rank(id));
                    if (p != nullptr && !listed)
                        entries = with_suite_entry(entries, p->slot);
                }
                std::string text;
                if (error.empty() && !format_autoload_list(entries, text))
                    error = "pldmgr cannot keep a list this long";
                if (error.empty())
                {
                    ConfigChange change;
                    change.autoload_list = text;
                    const Outcome saved = api.save_config(change);
                    if (!saved.ok)
                        error = saved.error;
                    read_config(api);
                }
            }
            Notice n;
            n.failed = !error.empty();
            n.title = error.empty() ? "The Signal suite starts with the console"
                                    : "The suite was not added to start-up";
            n.detail = error;
            notice(std::move(n));
            read_chain();
        });
}

void Store::wait_idle()
{
    worker_.wait_idle();
}

namespace
{

std::unique_ptr<Store> &store_slot()
{
    static std::unique_ptr<Store> slot;
    return slot;
}

} // namespace

Store &store()
{
    std::unique_ptr<Store> &slot = store_slot();
    if (!slot)
        slot = std::make_unique<Store>([] { return make_loopback_transport(kPldmgrPort); });
    return *slot;
}

void install_store(Store::TransportFactory factory, std::string root)
{
    store_slot() = std::make_unique<Store>(std::move(factory), std::move(root));
}

} // namespace hui::payloader
