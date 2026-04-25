#include "FileWatcher.h"
#include "Core/Log/Log.h"

#include <system_error>

namespace engine {

namespace fs = std::filesystem;

FileWatcher::FileWatcher(uint32_t intervalMs)
    : m_intervalMs(intervalMs)
    , m_nextCheck(std::chrono::steady_clock::now()) {}

void FileWatcher::watch(const std::string& path, Callback onChange) {
    std::error_code ec;
    auto mtime = fs::last_write_time(path, ec);
    if (ec) {
        FP_CORE_WARN("FileWatcher: cannot stat '{}' ({}) — not tracked", path, ec.message());
        return;
    }
    m_entries[path] = Entry{ mtime, std::move(onChange) };
    FP_CORE_TRACE("FileWatcher: tracking '{}'", path);
}

void FileWatcher::poll() {
    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextCheck) return;
    m_nextCheck = now + std::chrono::milliseconds(m_intervalMs);

    for (auto& [path, entry] : m_entries) {
        std::error_code ec;
        const auto mtime = fs::last_write_time(path, ec);
        if (ec) continue;  // transient (e.g. editor rewriting) — retry next sweep

        if (mtime != entry.lastWrite) {
            entry.lastWrite = mtime;
            FP_CORE_INFO("FileWatcher: change detected in '{}'", path);
            entry.onChange(path);
        }
    }
}

} // namespace engine
