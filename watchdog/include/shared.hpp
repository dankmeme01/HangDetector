#pragma once

#include <Windows.h>
#include <arc/prelude.hpp>
#include <matjson.hpp>
#include <matjson/reflect.hpp>

inline arc::Future<geode::Result<matjson::Value>> readJsonMessage(arc::TcpStream& stream) {
    char buf[8192];
    if (auto e = (co_await stream.receiveExact(buf, 4)).err()) {
        co_return geode::Err(e->message());
    }

    uint32_t lenVal = *reinterpret_cast<uint32_t*>(buf);
    if (lenVal > sizeof(buf)) {
        co_return geode::Err(fmt::format("JSON message too large: {} bytes", lenVal));
    }

    if (auto e = (co_await stream.receiveExact(buf, lenVal)).err()) {
        co_return geode::Err(e->message());
    }

    std::string_view str(reinterpret_cast<char*>(buf), lenVal);
    auto json = matjson::parse(str);
    if (!json) {
        co_return geode::Err(fmt::format("Failed to parse JSON from pipe: {}", json.unwrapErr()));
    }

    co_return geode::Ok(std::move(json.unwrap()));
}

inline arc::Future<geode::Result<>> writeJsonMessage(arc::TcpStream& stream, const matjson::Value& value) {
    auto str = value.dump(matjson::NO_INDENTATION);
    uint32_t len = str.size();
    std::vector<uint8_t> buf(4 + str.size());
    *reinterpret_cast<uint32_t*>(buf.data()) = len;
    std::memcpy(buf.data() + 4, str.data(), str.size());

    auto r = co_await stream.sendAll(buf.data(), buf.size());
    if (!r) {
        co_return geode::Err(r.unwrapErr().message());
    }

    co_return geode::Ok();
}
