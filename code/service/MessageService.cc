#include "MessageService.hpp"

namespace chatter::service {

[[nodiscard]] Task<resp::HttpResponse> MessageService::fetchUserMessage(
                        const JettyCat::chat::userId jwt_user_id,
                        const std::string& sender_id_str,
                        const std::string& receiver_id_str,
                        const std::string& offset_str) const {
    resp::HttpResponse http_response;

    // ---------- senderId 必填 ----------
    if (sender_id_str.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'senderId'");
        co_return http_response;
    }

    // ---------- 基础参数解析 ----------
    JettyCat::chat::userId sender_id = 0;
    JettyCat::chat::userId receiver_id = 0;
    size_t offset = 0;
    std::string param_err;
    try {
        sender_id = std::stoll(sender_id_str);
        if (!offset_str.empty()) {
            long off = std::stol(offset_str);
            if (off < 0) {
                param_err = "BAD_REQUEST: 'offset' must be non-negative";
            } else {
                offset = static_cast<size_t>(off);
            }
        }
    } catch (const std::exception&) {
        param_err = "BAD_REQUEST: Invalid parameter value";
    }
    if (!param_err.empty()) {
        http_response.setCode(400).setMsg(param_err);
        co_return http_response;
    }

    // ---------- receiverId 规则 ----------
    // 未提供 receiverId：接收者固定为当前登录用户（拉"对方发给我的"消息）
    if (receiver_id_str.empty()) {
        receiver_id = jwt_user_id;
    } else {
        // 提供 receiverId 时，仅允许拉"我自己发给对方"（senderId 必须等于当前登录用户）
        try {
            long long recv = std::stoll(receiver_id_str);
            if (recv <= 0) {
                param_err = "BAD_REQUEST: Invalid 'receiverId'";
            } else {
                receiver_id = static_cast<JettyCat::chat::userId>(recv);
            }
        } catch (const std::exception&) {
            param_err = "BAD_REQUEST: Invalid 'receiverId'";
        }
        if (!param_err.empty()) {
            http_response.setCode(400).setMsg(param_err);
            co_return http_response;
        }
        if (sender_id != jwt_user_id) {
            http_response.setCode(403).setMsg(
                "FORBIDDEN: 'receiverId' is only allowed when senderId is yourself");
            co_return http_response;
        }
    }

    // ---------- 拉取消息 ----------
    JettyCat::chat::MessageList message_list;
    switch (co_await m_message_dao->fetchFromInbox(sender_id, receiver_id, offset, message_list)) {
    case JettyCat::chat::DBState::SUCCESS: {
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : message_list) {
            messages.push_back(nlohmann::json::parse(msg.dump()));
        }
        nlohmann::json data;
        data["messages"] = messages;
        data["count"]    = message_list.size();
        http_response.setCode(200).setMsg("ok").setData(data);
        break;
    }
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        break;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        break;
    }
    co_return http_response;
}


[[nodiscard]] Task<resp::HttpResponse> MessageService::fetchGroupMessage(
                        const std::string& group_id_str,
                        const std::string& offset_str) const {
    resp::HttpResponse http_response;

    // ---------- groupId 必填 ----------
    if (group_id_str.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'groupId'");
        co_return http_response;
    }

    // ---------- 参数解析 ----------
    JettyCat::chat::groupId group_id = 0;
    size_t offset = 0;
    std::string param_err;
    try {
        group_id = std::stoi(group_id_str);
        if (!offset_str.empty()) {
            long off = std::stol(offset_str);
            if (off < 0) {
                param_err = "BAD_REQUEST: 'offset' must be non-negative";
            } else {
                offset = static_cast<size_t>(off);
            }
        }
    } catch (const std::exception&) {
        param_err = "BAD_REQUEST: Invalid parameter value";
    }
    if (!param_err.empty()) {
        http_response.setCode(400).setMsg(param_err);
        co_return http_response;
    }

    // ---------- 拉取消息 ----------
    JettyCat::chat::MessageList message_list;
    switch (co_await m_message_dao->fetchFromGroup(group_id, offset, message_list)) {
    case JettyCat::chat::DBState::SUCCESS: {
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : message_list) {
            messages.push_back(nlohmann::json::parse(msg.dump()));
        }
        nlohmann::json data;
        data["messages"] = messages;
        data["count"]    = message_list.size();
        http_response.setCode(200).setMsg("ok").setData(data);
        break;
    }
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        break;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        break;
    }
    co_return http_response;
}

} // namespace chatter::service
