#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

// Polling file watcher. Tracks a set of files and invokes a callback when
// their last-write-time changes. Single-threaded: the owner calls poll()
// from the main loop; the watcher keeps its own "next check" deadline and
// short-circuits cheap calls until the interval elapses.
//
// Why polling (not OS-native change notifications): portable, trivial to
// reason about, zero platform code. At 250 ms we stat a handful of files
// per second — negligible versus a frame.
class FileWatcher {
public:
    using Callback = std::function<void(const std::string& path)>;

    // 'intervalMs' is the minimum gap between two stat sweeps.
    explicit FileWatcher(uint32_t intervalMs = 250);

    // Start tracking 'path'. The initial mtime is captured now, so the first
    // change detected is a real post-registration modification. Silently
    // ignored if the file does not exist.
    void watch(const std::string& path, Callback onChange);

    // Must be called regularly from the main thread. No-op until the poll
    // interval has elapsed since the last sweep.
    void poll();

private:
    struct Entry {
        std::filesystem::file_time_type lastWrite;
        Callback                        onChange;
    };

    uint32_t                                 m_intervalMs;
    std::chrono::steady_clock::time_point    m_nextCheck;
    std::unordered_map<std::string, Entry>   m_entries;
};

} // namespace engine
