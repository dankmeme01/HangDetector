#include <Geode/Geode.hpp>
#include <shared.hpp>

using namespace geode::prelude;

struct GameState {
    std::string scene, layer;
    asp::Instant lastUpdate;
};

static asp::SpinLock<GameState> g_state;

PVOID GeodeFunctionTableAccess64(HANDLE hProcess, DWORD64 AddrBase);

struct GlobalWatcher : public CCObject {
    GlobalWatcher() {
        CCScheduler::get()->scheduleSelector(schedule_selector(GlobalWatcher::update), this, 0.0f, false);
    }

    void update(float dt) {
        auto scene = CCScene::get();
        auto state = g_state.lock();

        if (!scene) {
            state->scene.clear();
            state->layer.clear();
            return;
        }

        state->scene = typeid(*scene).name();
        auto layer = scene->getChildByType(0);
        if (layer) {
            state->layer = typeid(*layer).name();
        } else {
            state->layer.clear();
        }
        state->lastUpdate = asp::Instant::now();
    }
};

arc::Future<matjson::Value> handleRequest(matjson::Value input) {
    auto ty = input["type"].asString().unwrapOrDefault();

    if (ty == "ping") {
        co_return matjson::makeObject({
            {"type", "pong"},
        });
    } else if (ty == "status") {
        auto state = g_state.lock();
        co_return matjson::makeObject({
            {"type", "status"},
            {"scene", state->scene},
            {"layer", state->layer},
            {"sinceUpdate", state->lastUpdate.elapsed().millis()},
        });
    }

    co_return matjson::makeObject({
        {"type", "error"},
        {"error", "Unknown request type"},
    });
}

arc::Future<> pipeFunc() {
    while (true) {
        auto res = co_await acceptPipeConnection();
        if (!res) {
            log::warn("Failed to accept pipe connection: {}", res.unwrapErr());
            continue;
        }

        auto pipe = std::move(res.unwrap());
        log::info("Watchdog connected to our pipe!");

        while (true) {
            auto json = co_await readJsonMessage(pipe);
            if (!json) {
                log::warn("Failed to read JSON message: {}", json.unwrapErr());
                break;
            }

            auto result = co_await handleRequest(json.unwrap());
            auto res = co_await writeJsonMessage(pipe, result);
            if (!res) {
                log::warn("Failed to write JSON message: {}", res.unwrapErr());
                break;
            }
        }
    }
}

$on_mod(Loaded) {
    auto exe = Mod::get()->getResourcesDir() / "watchdog.exe";
    if (!std::filesystem::exists(exe)) {
        log::error("watchdog.exe not found in {}", exe);
        return;
    }

    auto s = utils::string::pathToString(exe);
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    auto logPath = utils::string::pathToString(Mod::get()->getConfigDir() / "watchdog.log");

    auto cmd = fmt::format(
        "\"{}\" {} \"{}\" {}",
        s,
        GetCurrentProcessId(),
        logPath,
        (uint64_t)&GeodeFunctionTableAccess64
    );

    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        log::error("Failed to launch watchdog.exe: {}", GetLastError());
        return;
    }

    // let it run
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    arc::spawn(pipeFunc());

    // create a watcher
    new GlobalWatcher();
}
