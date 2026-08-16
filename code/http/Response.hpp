// HttpResponse.hpp —— 统一的 HTTP 响应 class（风格对齐 WsMessage）
//
// 目的：把各 handler 散落的响应拼装收敛成一个标准形状：
//   成功：   {"code":200,"status":"success","msg":"ok","data":...}
//   业务失败：{"code":400,"status":"failed","msg":"...","data":null}
//   服务端错误：{"code":500,"status":"error","msg":"...","data":null}
//
// 使用方式与 WsMessage 一致：默认构造 + 链式 setter + getter + dump()。
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace chatter::resp {

class HttpResponse {
public:
    HttpResponse() = default;
    ~HttpResponse() = default;

    // ---- 链式 setter ----
    HttpResponse& setCode(int code)              { m_code = code; return *this; }
    HttpResponse& setMsg(const std::string& msg) { m_msg = msg;  return *this; }
    HttpResponse& setData(const nlohmann::json& data) { m_data = data; return *this; }

    // ---- getter ----
    [[nodiscard]] int getCode() const { return m_code; }
    [[nodiscard]] const std::string& getMsg() const { return m_msg; }
    [[nodiscard]] bool isOk() const { return m_code >= 200 && m_code < 300; }

    // ---- 序列化 ----
    // status 字段由 code 自动推导：2xx=success / 4xx=failed / 5xx=error
    [[nodiscard]] nlohmann::json toJson() const {
        nlohmann::json j;
        j["code"]   = m_code;
        j["status"] = m_code >= 500 ? "error" : (m_code >= 400 ? "failed" : "success");
        j["msg"]    = m_msg;
        j["data"]   = m_data;
        return j;
    }
    [[nodiscard]] std::string dump() const { return toJson().dump(); }

private:
    int m_code{200};             // 复用 http 状态码
    std::string m_msg{"ok"};     // 人类可读描述
    nlohmann::json m_data{};     // 业务数据（默认 null）
};

} // namespace chatter::resp
