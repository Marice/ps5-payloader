// PayLoader - One background thread that runs jobs in order, so no request blocks a frame.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "payloader/worker.hpp"

#include <utility>

namespace hui::payloader
{

JobWorker::JobWorker()
{
    // Never name threads: pthread_setname_np hangs on the console.
    started_ = pthread_create(&thread_, nullptr, &JobWorker::thread_main, this) == 0;
}

JobWorker::~JobWorker()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (started_)
        pthread_join(thread_, nullptr);
}

void *JobWorker::thread_main(void *self)
{
    static_cast<JobWorker *>(self)->run();
    return nullptr;
}

void JobWorker::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;)
    {
        wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (stopping_)
            return;
        std::function<void()> job = std::move(jobs_.front());
        jobs_.pop_front();
        working_ = true;
        lock.unlock();
        job();
        lock.lock();
        working_ = false;
        if (jobs_.empty())
            idle_.notify_all();
    }
}

void JobWorker::queue(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

std::size_t JobWorker::waiting() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

void JobWorker::wait_idle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return jobs_.empty() && !working_; });
}

} // namespace hui::payloader
