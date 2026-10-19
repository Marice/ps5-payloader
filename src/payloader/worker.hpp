// PayLoader - One background thread that runs jobs in order, so no request blocks a frame.
// SPDX-License-Identifier: GPL-3.0-or-later
//
//   JobWorker worker;
//   worker.queue([&] { slow_request(); });
//   worker.wait_idle(); // tests only

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <pthread.h>

namespace hui::payloader
{

class JobWorker
{
  public:
    JobWorker();
    // Finishes the running job, drops the rest, and joins.
    ~JobWorker();
    JobWorker(const JobWorker &) = delete;
    JobWorker &operator=(const JobWorker &) = delete;

    void queue(std::function<void()> job);
    // Jobs waiting, not counting the one running.
    std::size_t waiting() const;
    void wait_idle();

  private:
    static void *thread_main(void *self);
    void run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> jobs_;
    bool working_ = false;
    bool stopping_ = false;
    bool started_ = false;
    pthread_t thread_{};
};

} // namespace hui::payloader
