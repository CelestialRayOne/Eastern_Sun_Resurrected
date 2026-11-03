#pragma once
// Debug lines for the plugin log. Nothing happens until the plugin sets the sink (plugin.cpp does, at load).
#include <functional>
#include <mutex>
#include <set>
#include <string>

namespace esrwiki {

inline std::function<void(const std::string&)>& DebugSink() {
    static std::function<void(const std::string&)> sink;
    return sink;
}

inline void DebugLog(const std::string& message) {
    if (DebugSink()) DebugSink()("[debug] " + message);
}

// Logs a message once per key, so a paint loop does not flood the log.
inline void DebugLogOnce(const std::string& key, const std::string& message) {
    static std::mutex lock;
    static std::set<std::string> seen;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!seen.insert(key).second) return;
    }
    DebugLog(message);
}

}  // namespace esrwiki
