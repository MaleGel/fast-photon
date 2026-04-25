#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace engine {

// Lightweight task-based job system.
//
// Two flavours of work intake:
//
//   submit(F)            — single-shot job, returns a JobHandle the caller
//                          can wait() on.
//   parallel_for(...)    — splits a [begin, end) range into N chunks and
//                          submits one job per chunk, returning a single
//                          JobHandle that completes when all chunks do.
//
// wait() participates in work-stealing: the calling thread runs jobs from
// the queue while the target group is still in flight. This means main
// can submit work and immediately wait without leaving N+1 threads idle.
//
// Concurrency policy:
//   - One mutex-protected queue (simple; sufficient for ≤ 8 workers).
//   - Workers spin on the queue's condition_variable; wait() pulls from
//     the same queue until its handle's counter hits zero.
//   - Jobs are std::function<void()> — heap allocation per submit, but
//     measurable cost only for jobs that *should* be inlined (tiny ECS
//     loops). Acceptable for the current scale.
//
// Thread-safety:
//   - submit / parallel_for / wait may be called from any thread.
//   - shutdown() must be the last call; no jobs may be submitted after.
class JobSystem {
public:
    using Job = std::function<void()>;

    // Spawns 'workerCount' worker threads. Pass 0 to use
    // (hardware_concurrency - 1), clamped to at least 1.
    static void init(uint32_t workerCount = 0);

    // Joins all workers. Pending jobs must already be drained — caller is
    // responsible for that via wait() on outstanding handles.
    static void shutdown();

    // Returns the thread count actually spawned.
    static uint32_t workerCount() { return s_workerCount; }


    // Opaque handle returned by submit / parallel_for. Internally a tiny
    // refcounted control block tracking the number of in-flight jobs in
    // its group. shared_ptr makes copying / moving handles cheap and
    // well-defined.
    struct ControlBlock {
        // Number of jobs in this group that haven't finished yet. Reaches
        // zero exactly once, at which point waiters are released.
        std::atomic<uint32_t> remaining{0};
    };

    class JobHandle {
    public:
        JobHandle() = default;
        explicit JobHandle(std::shared_ptr<ControlBlock> cb) : m_cb(std::move(cb)) {}

        // True when every job in the group has completed. Cheap, no lock.
        bool done() const {
            return !m_cb || m_cb->remaining.load(std::memory_order_acquire) == 0;
        }

        // Block until done(). Steals jobs from the queue while waiting so
        // we don't leave the calling thread idle.
        void wait() const;

    private:
        std::shared_ptr<ControlBlock> m_cb;
    };

    // Submit a single job. Returns a handle that completes when the job runs.
    static JobHandle submit(Job job);

    // Split [begin, end) into chunks of about 'grainSize' (auto-picked when
    // 0) and submit one job per chunk, calling fn(i) for each i in range.
    // Returns one handle for the whole group.
    //
    // 'fn' must be safe to call concurrently — i.e. each iteration writes
    // only to its own data, not shared mutable state.
    static JobHandle parallel_for(size_t begin, size_t end,
                                  std::function<void(size_t)> fn,
                                  size_t grainSize = 0);

    // Identifies the thread that called init(). Useful for asserting that
    // certain APIs (e.g. Profiler::pushScope) only run on main.
    static bool isMainThread();

private:
    struct Task {
        Job                            run;
        std::shared_ptr<ControlBlock>  group;
    };

    static void workerLoop();
    static void runOne(Task&& task);

    // Returns true if a task was popped + executed; false if the queue is
    // empty. wait() loops on this until its handle hits zero.
    static bool tryRunOne();

    // Try to pop without blocking.
    static bool tryPop(Task& out);

    static std::vector<std::thread>     s_workers;
    static std::queue<Task>             s_queue;
    static std::mutex                   s_mutex;
    static std::condition_variable      s_cv;
    static std::atomic<bool>            s_running;
    static uint32_t                     s_workerCount;
    static std::thread::id              s_mainThreadId;
};

} // namespace engine
