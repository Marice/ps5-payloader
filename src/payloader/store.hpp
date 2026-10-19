// PayLoader - What pldmgr holds, kept fresh in the background for the screens.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One worker thread asks pldmgr; screens read a copy that only changes in
// update(), so a frame never waits and never sees half a list. A screen says
// each frame what it shows (want()); only that is asked for again. Actions
// queue behind the reads and report back as notices for toasts.
//
//   Store &store = payloader::store();
//   store.want(Topic::payloads);
//   store.update(dt);
//   for (const Payload &p : store.view().payloads.items) draw(p);
//   store.launch(slot);
//   for (const Notice &n : store.take_notices()) toast(n);

#pragma once

#include "payloader/chain.hpp"
#include "payloader/pldmgr.hpp"
#include "payloader/worker.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hui::payloader
{

enum class Topic : unsigned char
{
    payloads,
    processes,
    autoload,
    repository,
    sources,
    count,
};

enum class Link : unsigned char
{
    checking, // not asked yet
    offline,  // nothing answers on 8084
    too_old,  // pldmgr answers, but older than 0.6.0
    unknown,  // something answers that is not pldmgr
    ready,
};

struct View
{
    Link link = Link::checking;
    std::string version; // pldmgr's, when it answered

    bool have_payloads = false;
    PayloadList payloads;
    bool have_config = false;
    Config config;
    bool have_processes = false;
    std::vector<Process> processes;
    bool have_autoload = false;
    AutoloadStatus autoload;
    bool have_repository = false;
    bool repository_busy = false; // a fetch or refresh is under way
    std::string repository_error;
    std::vector<RepoSource> repository;
    bool have_sources = false;
    std::vector<Source> sources;

    // Slots, pids and ids with an action under way, to show a spinner and refuse a second press.
    // The boot chain (read from the console's files, also without pldmgr).
    bool have_chain = false;
    Chain chain;
    AutoloaderList chain_list; // when chain.kind is autoloader

    std::set<std::string> busy;
    bool pldmgr_starting = false; // the app sent its pldmgr to the ELF loader and waits for it
};

struct Notice
{
    bool failed = false;
    std::string title;
    std::string detail;
    // An install that met the same payload from another repository; the screen
    // asks, then calls install() again with InstallMode::replace.
    bool conflict = false;
    RepoPayload payload;
    std::string source_url;
    std::string conflict_slot;
    std::string conflict_source;
    std::string conflict_version;
    // The whole suite is installed: the screen asks whether it should start with the console.
    bool suite_installed = false;
};

class Store
{
  public:
    using TransportFactory = std::function<std::unique_ptr<Transport>()>;
    // root prefixes the console's file paths (tests); the app passes "".
    explicit Store(TransportFactory factory, std::string root = {});
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // Call each frame for each topic the screen shows.
    void want(Topic topic);
    void update(float dt);
    const View &view() const
    {
        return shown_;
    }
    std::vector<Notice> take_notices();
    // Ask everything again now (a screen's "retry").
    void refresh();

    void launch(const Payload &payload);
    void remove(const Payload &payload);
    void set_favourite(const std::string &slot, bool favourite);
    void save_autoload(bool enabled, int delay_seconds, const std::string &list);
    void abort_autoload();
    void refresh_repository();
    void install(const RepoPayload &payload, const std::string &source_url, InstallMode mode,
                 const std::string &replace_slot);
    void add_source(const std::string &url);
    void remove_source(int index, const std::string &name);
    void stop(const Process &process);
    void save_settings(const ConfigChange &change);
    // ps5-autoloader's list (the chain must be the autoloader's).
    void save_chain(const std::vector<AutoloadEntry> &entries);
    // Copies an installed payload's file next to the list, then appends it.
    void add_to_chain(const Payload &payload);
    // Takes every line naming file out of the list; the file stays.
    void remove_from_chain(const std::string &file);
    // Starts the pldmgr this app carries: its own front-end off in its config, then the ELF loader.
    void start_pldmgr();
    // Tests: replaces the ELF loader (port 9021).
    void set_loader(std::function<bool(std::string_view)> loader);
    // The Signal suite: installs (or updates) the three from their repository, in order.
    void install_suite();
    // Puts the installed suite payloads in the boot chain, in suite order; listed ones stay put.
    void add_suite_to_chain();

    // Tests: blocks until the worker has nothing left to do.
    void wait_idle();

  private:
    void poll(float dt);
    void run(std::function<void(Pldmgr &)> job);
    void check_link(Pldmgr &api);
    bool linked();
    void note(const Outcome &outcome, const std::string &title);
    void notice(Notice notice);
    void read_payloads(Pldmgr &api);
    void read_config(Pldmgr &api);
    void mark_busy(const std::string &key, bool busy);

    void read_chain();
    void ensure_suite_source(Pldmgr &api);
    void ensure_front_end_off(Pldmgr &api);

    TransportFactory factory_;
    std::string root_;
    std::function<bool(std::string_view)> loader_;
    std::unique_ptr<Pldmgr> api_; // used on the worker thread only
    std::mutex mutex_;
    View state_; // written by the worker under mutex_
    std::uint64_t revision_ = 0;
    std::uint64_t shown_revision_ = ~0ull;
    View shown_;
    std::vector<Notice> notices_;

    // Read and written on the main thread only.
    std::array<float, static_cast<std::size_t>(Topic::count)> wanted_{};
    std::array<float, static_cast<std::size_t>(Topic::count)> since_{};
    float since_link_ = 1000.0f;
    float since_chain_ = 1000.0f;
    // Cleared by worker jobs to ask again; relinked_ asks for everything after a reconnect.
    std::atomic<bool> repository_asked_{false};
    std::atomic<bool> sources_asked_{false};
    std::atomic<bool> relinked_{false};
    bool suite_told_ = false; // worker thread only: the one-time "suite added" notice
    JobWorker worker_;        // last: it stops before the members its jobs use
};

// The app's store, made on first use.
Store &store();
// Replaces it, for tests.
void install_store(Store::TransportFactory factory, std::string root = {});

} // namespace hui::payloader
