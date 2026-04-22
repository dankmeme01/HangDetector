#pragma once
#include <shared.hpp>

static std::optional<std::ofstream> g_logFile;
static asp::Instant g_lastFlush = asp::Instant::now();

template <typename... Args>
void log(fmt::format_string<Args...> fmtStr, Args&&... args) {
    auto str = fmt::format(fmtStr, std::forward<Args>(args)...);
    fmt::println("{}", str);

    if (g_logFile) {
        (*g_logFile) << str << '\n';

        if (g_lastFlush.elapsed() > asp::Duration::fromSecs(1)) {
            g_logFile->flush();
            g_lastFlush = asp::Instant::now();
        }
    }
}
