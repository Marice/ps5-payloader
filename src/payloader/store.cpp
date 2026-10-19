// PayLoader - What pldmgr holds, kept fresh in the background for the screens.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/store.hpp"

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

Store::Store(TransportFactory factory) : factory_(std::move(factory))
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

void install_store(Store::TransportFactory factory)
{
    store_slot() = std::make_unique<Store>(std::move(factory));
}

} // namespace hui::payloader
