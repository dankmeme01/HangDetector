#pragma once
#include <asp/fs.hpp>
#include <shared.hpp>

static std::optional<asp::fs::BufferedFile> g_logFile;
static asp::Instant g_lastFlush = asp::Instant::now();
static auto epoch = asp::Instant::now();

template <typename... Args>
void log(fmt::format_string<Args...> fmtStr, Args&&... args) {
    auto sinceEpoch = epoch.elapsed();

    auto str = fmt::format(fmtStr, std::forward<Args>(args)...);
    fmt::println("{}", str);

    if (g_logFile) {
        (void) g_logFile->write(fmt::format("[{:.4f}] {}\n", sinceEpoch.seconds<float>(), str));

        if (g_lastFlush.elapsed() > asp::Duration::fromSecs(1)) {
            (void) g_logFile->flush();
            g_lastFlush = asp::Instant::now();
        }
    }
}
