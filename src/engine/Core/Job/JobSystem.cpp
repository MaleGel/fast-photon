#include "JobSystem.h"
#include "Core/Log/Log.h"
#include "Core/Assert/Assert.h"

namespace engine {

// ── Static storage ──────────────────────────────────────────────────────────

std::vector<std::thread>      JobSystem::s_workers;
std::queue<JobSystem::Task>   JobSystem::s_queue;
std::mutex                    JobSystem::s_mutex;
std::condition_variable       JobSystem::s_cv;
std::atomic<bool>             JobSystem::s_running{false};
uint32_t                      JobSystem::s_workerCount = 0;
std::thread::id               JobSystem::s_mainThreadId;

// ── Init / shutdown ─────────────────────────────────────────────────────────

void JobSystem::init(uint32_t workerCount) {
    s_mainThreadId = std::this_thread::get_id();

    if (workerCount == 0) {
        const uint32_t hc = std::thread::hardware_concurrency();
        // -1 because the main thread also runs jobs via wait()'s steal loop,
        // so reserving every logical core for a worker would oversubscribe.
        workerCount = (hc > 1) ? (hc - 1) : 1;
    }
    s_workerCount = workerCount;

    s_running.store(true, std::memory_order_release);
    s_workers.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        s_workers.emplace_back(&JobSystem::workerLoop);
    }
    FP_CORE_INFO("JobSystem initialized ({} workers)", workerCount);
}

void JobSystem::shutdown() {
    {
        // Flip the running flag inside the lock so workers waking up on
        // the cv see a consistent state.
        std::lock_guard<std::mutex> lk(s_mutex);
        s_running.store(false, std::memory_order_release);
    }
    s_cv.notify_all();
    for (auto& t : s_workers) {
        if (t.joinable()) t.join();
    }
    s_workers.clear();

    // After joining there should be no pending tasks — the caller is
    // contractually responsible for waiting on every handle before
    // shutdown.
    if (!s_queue.empty()) {
        FP_CORE_WARN("JobSystem: shutdown with {} queued task(s) — these are dropped",
                     s_queue.size());
        std::queue<Task> empty;
        s_queue.swap(empty);
    }

    s_workerCount = 0;
    FP_CORE_TRACE("JobSystem destroyed");
}

bool JobSystem::isMainThread() {
    return std::this_thread::get_id() == s_mainThreadId;
}

// ── Submission ──────────────────────────────────────────────────────────────

JobSystem::JobHandle JobSystem::submit(Job job) {
    auto cb = std::make_shared<ControlBlock>();
    cb->remaining.store(1, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lk(s_mutex);
        s_queue.push(Task{ std::move(job), cb });
    }
    s_cv.notify_one();
    return JobHandle{ std::move(cb) };
}

JobSystem::JobHandle JobSystem::parallel_for(size_t begin, size_t end,
                                             std::function<void(size_t)> fn,
                                             size_t grainSize) {
    auto cb = std::make_shared<ControlBlock>();

    if (begin >= end) {
        // Empty range — return an already-completed handle so callers can
        // unconditionally wait().
        return JobHandle{ cb };
    }

    const size_t total = end - begin;

    // Auto grain: aim for 4 chunks per worker (so faster threads can pick
    // up extra work). At least 1 element per chunk.
    if (grainSize == 0) {
        const size_t targetChunks = (s_workerCount > 0 ? s_workerCount : 1) * 4;
        grainSize = std::max<size_t>(1, total / targetChunks);
    }

    const size_t chunkCount = (total + grainSize - 1) / grainSize;
    cb->remaining.store(static_cast<uint32_t>(chunkCount),
                        std::memory_order_release);

    {
        std::lock_guard<std::mutex> lk(s_mutex);
        for (size_t c = 0; c < chunkCount; ++c) {
            const size_t chunkBegin = begin + c * grainSize;
            const size_t chunkEnd   = std::min(chunkBegin + grainSize, end);
            // Capture fn by value (shared_ptr-style ref through std::function);
            // copy is cheap because std::function is itself a fat pointer.
            s_queue.push(Task{
                [fn, chunkBegin, chunkEnd]() {
                    for (size_t i = chunkBegin; i < chunkEnd; ++i) fn(i);
                },
                cb,
            });
        }
    }
    s_cv.notify_all();
    return JobHandle{ std::move(cb) };
}

// ── Waiting ─────────────────────────────────────────────────────────────────

void JobSystem::JobHandle::wait() const {
    if (!m_cb) return;
    // Steal-loop: while the group still has work in flight, run any task
    // we can find. This keeps the calling thread productive instead of
    // blocking with workers idle for queue items it could process itself.
    while (m_cb->remaining.load(std::memory_order_acquire) > 0) {
        if (!JobSystem::tryRunOne()) {
            // Queue is empty but our group hasn't finished — must be a job
            // currently executing on a worker. Yield rather than spin hot.
            std::this_thread::yield();
        }
    }
}

// ── Worker loop ─────────────────────────────────────────────────────────────

void JobSystem::workerLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lk(s_mutex);
            s_cv.wait(lk, [] {
                return !s_queue.empty()
                    || !s_running.load(std::memory_order_acquire);
            });
            if (!s_running.load(std::memory_order_acquire) && s_queue.empty()) {
                return;
            }
            task = std::move(s_queue.front());
            s_queue.pop();
        }
        runOne(std::move(task));
    }
}

bool JobSystem::tryRunOne() {
    Task task;
    if (!tryPop(task)) return false;
    runOne(std::move(task));
    return true;
}

bool JobSystem::tryPop(Task& out) {
    std::lock_guard<std::mutex> lk(s_mutex);
    if (s_queue.empty()) return false;
    out = std::move(s_queue.front());
    s_queue.pop();
    return true;
}

void JobSystem::runOne(Task&& task) {
    // Run the user code, then decrement the group's counter. If we go to
    // zero, this was the last job — no extra notification needed because
    // wait() spins on the atomic with yield, no cv to wake.
    try {
        task.run();
    } catch (const std::exception& exc) {
        // We can't propagate to the submitter; log and continue so the
        // counter still gets decremented (otherwise wait() would deadlock).
        FP_CORE_ERROR("JobSystem: job threw: {}", exc.what());
    } catch (...) {
        FP_CORE_ERROR("JobSystem: job threw non-std exception");
    }
    if (task.group) {
        task.group->remaining.fetch_sub(1, std::memory_order_acq_rel);
    }
}

} // namespace engine
