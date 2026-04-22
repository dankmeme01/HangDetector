#include <shared.hpp>
#include "Winapi.hpp"

using namespace arc;
using namespace asp::time;

static int g_pid;
static HANDLE g_parent;
static FnTableAccess g_tableAccessFn;

Future<std::optional<DWORD>> waitForExit(Duration deadline) {
    DWORD waitResult = co_await arc::spawnBlocking<DWORD>([&] {
        return WaitForSingleObject(g_parent, deadline.millis());
    });

    if (waitResult == WAIT_OBJECT_0) {
        DWORD ec;
        if (GetExitCodeProcess(g_parent, &ec)) {
            co_return ec;
        }
    }

    co_return std::nullopt;
}

Future<> handleDeath() {
    log("Game likely died, giving 2.5 seconds to terminate and checking exit status..");
    auto ec = co_await waitForExit(*Duration::fromSecs(2.5));

    if (ec) {
        log("Game exited with code {}, watchdog will terminate. Goodbye!", *ec);
        co_return;
    }

    log("Hang detected!");

    auto mtId = findMainThread(g_pid);
    log("Detected main thread ID as {}", mtId);

    auto thrd = OpenThread(THREAD_ALL_ACCESS, FALSE, mtId);
    if (!thrd) {
        log("Failed to open main thread: {}", GetLastError());
        co_return;
    }

    log("Dumping stack trace...");
    auto frames = dumpStackTrace(g_parent, thrd);
    for (const auto& frame : frames) {
        log("- {}", frame);
    }

    log("Forcing a fault on main thread");
    forceFault(g_parent, thrd);

    CloseHandle(thrd);

    log("Showing a message box");

    auto content = fmt::format(
        "Geometry Dash has been detected having a background hang, meaning the game window was closed, but the process is still running, just completely unresponsive.\n"
        "This is likely due to a bug in Geode or one of the mods you have installed. The information below can be very helpful for mod developers to diagnose the issue.\n\n"
        "You are seeing this because of the Hang Detector mod, which now killed the game and collected this information.\n\n"
        "Stack trace:\n"
    );

    for (const auto& frame : frames) {
        content += fmt::format("- {}\n", frame);
    }

    MessageBoxA(nullptr, content.c_str(), "Game Hang Detected", MB_ICONERROR | MB_OK);

    log("Goodbye!");
}

Future<Result<>> asyncMain(int argc, const char** argv) {
    if (argc < 4) {
        co_return Err("bad invocation");
    }

    // open the parent process
    std::string_view pidstr(argv[1]);
    std::from_chars(pidstr.data(), pidstr.data() + pidstr.size(), g_pid);
    g_parent = OpenProcess(SYNCHRONIZE | PROCESS_ALL_ACCESS, FALSE, g_pid);
    if (!g_parent) {
        co_return Err(fmt::format("Failed to open parent process: {}", GetLastError()));
    }

    // open the log file
    auto logPath = std::string(argv[2]);
    g_logFile.emplace(logPath);

    // set this shit
    std::string_view fnAddrStr(argv[3]);
    uint64_t tptr;
    std::from_chars(fnAddrStr.data(), fnAddrStr.data() + fnAddrStr.size(), tptr);
    g_tableAccessFn = reinterpret_cast<FnTableAccess>(tptr);

    // open the pipe
    auto pipe = ARC_CO_UNWRAP(co_await connectToPipe());

    bool beenInMenu = false;

    while (true) {
        auto wres = co_await writeJsonMessage(pipe, matjson::makeObject({
            {"type", "status"},
        }));
        if (!wres) {
            log("Failed to write JSON message: {}", wres.unwrapErr());
            co_await handleDeath();
            break;
        }

        auto tres = co_await arc::timeout(Duration::fromMillis(500), readJsonMessage(pipe));
        if (!tres) {
            log("Timed out waiting for JSON message");
            co_await handleDeath();
            break;
        }

        auto res = tres.unwrap();
        if (!res) {
            log("Failed to read JSON message: {}", res.unwrapErr());
            co_await handleDeath();
            break;
        }

        auto json = std::move(res).unwrap();
        auto layer = json["layer"].asString().unwrapOrDefault();

        if (layer.contains("MenuLayer") && !beenInMenu) {
            beenInMenu = true;
            // on first menulayer entry, dump all loaded modules for symbolication purposes
            initCachedModules(g_pid);
        }

        // nothing is happening, so wait a bit
        co_await arc::sleep(Duration::fromMillis(50));
    }

    CloseHandle(g_parent);
    g_logFile.reset();

    co_return Ok();
}

ARC_DEFINE_MAIN_NT(asyncMain, 4);