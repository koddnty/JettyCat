#include "chatter.hpp"
#include "login/tools.hpp"

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
            // content 可能是内层 Message 的 JSON 字符串(文档格式), 也可能是 JSON 对象。
            // 若直接 dump() 会对字符串再加一层引号, 导致后端 Message::load 解析时丢失内容。
            m_content = json["content"].is_string() ? json["content"].get<std::string>() : json["content"].dump();
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


void registeUrl(const http::HttpServer::ptr& server, const websocket::WsServer::ptr& ws_server) {
    server->GET("/chat/fetch_user_message", co_FetchUserMessage);
    server->GET("/chat/fetch_group_message", co_FetchGroupMessage);
    ChatWebSocketServer::registeUrl(ws_server);
}


Task<void> co_FetchUserMessage(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // JWT身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid or missing JWT token";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 获取请求参数
    std::string sender_id_str = req->getParam("senderId");
    std::string receiver_id_str = req->getParam("receiverId");
    std::string offset_str = req->getParam("offset");

    if (sender_id_str.empty()) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Missing or invalid 'senderId'";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    JettyCat::chat::userId sender_id = 0;
    JettyCat::chat::userId receiver_id = 0;
    size_t offset = 0;
    std::string param_err;
    try {
        sender_id = std::stoi(sender_id_str);
        if (!offset_str.empty()) {
            long off = std::stol(offset_str);
            if (off < 0) {
                param_err = "BAD_REQUEST: 'offset' must be non-negative";
            } else {
                offset = static_cast<size_t>(off);
            }
        }
    } catch (const std::exception& e) {
        param_err = "BAD_REQUEST: Invalid parameter value";
    }

    if (!param_err.empty()) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = param_err;
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    // 从JWT中解析当前用户身份
    JettyCat::chat::userId jwt_user_id = JWT::parserPayload(jwt).user_id;

    // 未提供 receiverId 时, 接收者固定为当前登录用户 (拉取"对方发给我的"消息)
    if (receiver_id_str.empty()) {
        receiver_id = jwt_user_id;
    } else {
        // 提供 receiverId 时, 仅允许拉取"我自己发给对方"的消息 (senderId 必须等于当前登录用户)
        try {
            long recv = std::stol(receiver_id_str);
            if (recv <= 0) {
                param_err = "BAD_REQUEST: Invalid 'receiverId'";
            } else {
                receiver_id = static_cast<JettyCat::chat::userId>(recv);
            }
        } catch (const std::exception& e) {
            param_err = "BAD_REQUEST: Invalid 'receiverId'";
        }
        if (sender_id != jwt_user_id) {
            nlohmann::json j;
            j["status"] = "failed";
            j["error"] = "FORBIDDEN: 'receiverId' is only allowed when senderId is yourself";
            resp->appendHeader("Content-Type", "application/json");
            resp->setBody(j.dump());
            resp->setStatus(http::StatusCode::forbidden);
            co_await session->co_sendResp();
            co_return;
        }
    }

    if (!param_err.empty()) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = param_err;
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    // 从数据库拉取消息 (sender_id -> receiver_id)
    JettyCat::chat::MessageList message_list;
    JettyCat::chat::State state = co_await JettyCat::chat::fetchFromInbox(sender_id, receiver_id, offset, message_list);

    // 构建响应
    nlohmann::json resp_json;
    if (state == JettyCat::chat::State::SUCCESS) {
        resp_json["status"] = "success";
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : message_list) {
            messages.push_back(nlohmann::json::parse(msg.dump()));
        }
        resp_json["messages"] = messages;
        resp_json["count"] = message_list.size();
        resp->setStatus(http::StatusCode::ok);
    } else if (state == JettyCat::chat::State::TIMEOUT) {
        resp_json["status"] = "error";
        resp_json["error"] = "Database query timeout";
        resp->setStatus(http::StatusCode::internal_server_error);
    } else {
        resp_json["status"] = "error";
        resp_json["error"] = "Database query failed";
        resp->setStatus(http::StatusCode::internal_server_error);
    }

    resp->appendHeader("Content-Type", "application/json");
    resp->setBody(resp_json.dump());
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_FetchGroupMessage(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // JWT身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid or missing JWT token";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 获取请求参数
    std::string group_id_str = req->getParam("groupId");
    std::string offset_str = req->getParam("offset");

    if (group_id_str.empty()) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Missing or invalid 'groupId'";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

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
    } catch (const std::exception& e) {
        param_err = "BAD_REQUEST: Invalid parameter value";
    }

    if (!param_err.empty()) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = param_err;
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    // 从数据库拉取消息
    JettyCat::chat::MessageList message_list;
    JettyCat::chat::State state = co_await JettyCat::chat::fetchFromGroup(group_id, offset, message_list);

    // 构建响应
    nlohmann::json resp_json;
    if (state == JettyCat::chat::State::SUCCESS) {
        resp_json["status"] = "success";
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : message_list) {
            messages.push_back(nlohmann::json::parse(msg.dump()));
        }
        resp_json["messages"] = messages;
        resp_json["count"] = message_list.size();
        resp->setStatus(http::StatusCode::ok);
    } else if (state == JettyCat::chat::State::TIMEOUT) {
        resp_json["status"] = "error";
        resp_json["error"] = "Database query timeout";
        resp->setStatus(http::StatusCode::internal_server_error);
    } else {
        resp_json["status"] = "error";
        resp_json["error"] = "Database query failed";
        resp->setStatus(http::StatusCode::internal_server_error);
    }

    resp->appendHeader("Content-Type", "application/json");
    resp->setBody(resp_json.dump());
    co_await session->co_sendResp();
    co_return;
}

}
