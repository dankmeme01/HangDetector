#pragma once

#include <Windows.h>
#include <arc/prelude.hpp>
#include <matjson.hpp>
#include <matjson/reflect.hpp>

constexpr auto PIPE_NAME = L"\\\\.\\pipe\\dankmeme.why-did-it-hang";

inline arc::Future<geode::Result<arc::IocpPipe>> acceptPipeConnection() {
    auto pipe = CreateNamedPipeW(
        PIPE_NAME,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        4096,
        4096,
        NMPWAIT_USE_DEFAULT_WAIT,
        nullptr
    );

    if (pipe == INVALID_HANDLE_VALUE) {
        co_return geode::Err(fmt::format("Failed to create named pipe: {}", GetLastError()));
    }

    ARC_CO_UNWRAP_INTO(auto p, co_await arc::IocpPipe::listen(pipe));
    co_return Ok(std::move(p));
}

inline arc::Future<geode::Result<arc::IocpPipe>> connectToPipe() {
    while (true) {
        auto r = arc::IocpPipe::open(PIPE_NAME);
        if (!r) {
            co_await arc::sleep(asp::Duration::fromMillis(50));
            continue;
        }

        co_return Ok(std::move(*r));
    }
}

inline arc::Future<geode::Result<std::vector<uint8_t>>> readExact(arc::IocpPipe& pipe, size_t bytes) {
    std::vector<uint8_t> buf(bytes);
    size_t totalRead = 0;
    while (totalRead < bytes) {
        auto r = co_await pipe.read(buf.data() + totalRead, bytes - totalRead);
        if (!r) {
            co_return geode::Err(r.unwrapErr());
        }
        totalRead += *r;
    }

    co_return geode::Ok(std::move(buf));
}

inline arc::Future<geode::Result<matjson::Value>> readJsonMessage(arc::IocpPipe& pipe) {
    ARC_CO_UNWRAP_INTO(auto len, co_await readExact(pipe, 4));

    uint32_t lenVal = *reinterpret_cast<uint32_t*>(len.data());
    ARC_CO_UNWRAP_INTO(auto data, co_await readExact(pipe, lenVal));

    std::string_view str(reinterpret_cast<char*>(data.data()), data.size());
    auto json = matjson::parse(str);
    if (!json) {
        co_return geode::Err(fmt::format("Failed to parse JSON from pipe: {}", json.unwrapErr()));
    }

    co_return geode::Ok(std::move(json.unwrap()));
}

inline arc::Future<geode::Result<>> writeJsonMessage(arc::IocpPipe& pipe, const matjson::Value& value) {
    auto str = value.dump(matjson::NO_INDENTATION);
    uint32_t len = str.size();
    std::vector<uint8_t> buf(4 + str.size());
    *reinterpret_cast<uint32_t*>(buf.data()) = len;
    std::memcpy(buf.data() + 4, str.data(), str.size());

    size_t totalWritten = 0;
    while (totalWritten < buf.size()) {
        auto r = co_await pipe.write(buf.data() + totalWritten, buf.size() - totalWritten);
        if (!r) {
            co_return geode::Err(r.unwrapErr());
        }
        totalWritten += *r;
    }

    co_return geode::Ok();
}
