#include "chatter.hpp"

namespace chatter {

static Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");
int WsMessage::load(const nlohmann::json& json) {
    int status = -1;
    try {
        if (json.contains("code") && json["code"].is_number_integer()) {
            m_code = static_cast<http::StatusCode>(json["code"].get<int>());
        }
        if (json.contains("from") && json["from"].is_number_integer()) {
            m_from = json["from"].get<JettyCat::chat::userId>();
        }
        if (json.contains("to") && json["to"].is_number_integer()) {
            m_to = json["to"].get<JettyCat::chat::userId>();
        }
        if (json.contains("type") && json["type"].is_string()) {
            m_type = json["type"].get<std::string>();
        }
        // "reson"(dump()的拼写) "reason"(未来的合理拼写)
        if (json.contains("reason") && json["reason"].is_string()) {
            m_reason = json["reason"].get<std::string>();
        } else if (json.contains("reson") && json["reson"].is_string()) {
            m_reason = json["reson"].get<std::string>();
        }
        if (json.contains("content")) {
            m_content = json["content"].get<nlohmann::json>().dump();
        }
        status = 0;
        m_state = State::NORMAL;
    } catch (std::exception& e) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to paser message: " << e.what();
        status = -1;
        m_state = State::BAD;
    }
    return status;
}

int WsMessage::load(const std::string& raw) {
    try {
        nlohmann::json json = nlohmann::json::parse(raw);
        return load(json);
    } catch (const std::exception& e) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to parse ws message: " << e.what();
        m_state = State::BAD;
        return -1;
    }
}

WsMessage& WsMessage::setStatusCode(const http::StatusCode status_code, const std::string& reason) {
    m_state = State::NORMAL;
    m_code = status_code;
    if (reason.empty()) {
        m_reason = http::toString(status_code);
    }
    return *this;
}

std::string WsMessage::dump() {
    nlohmann::json json;
    json["code"] = m_code;
    json["type"] = m_type;
    json["reason"] = m_reason;
    json["content"] = m_content;
    json["from"] = m_from;
    json["to"] = m_to;
    return json.dump();
}
}