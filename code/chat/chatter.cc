#include "chatter.hpp"
#include "login/tools.hpp"
#include "dao/DbProvider.hpp"
#include "dao/FriendDao.hpp"
#include "dao/GroupDao.hpp"
#include "dao/MessageDao.hpp"
#include "service/FriendService.hpp"
#include "service/GroupService.hpp"
#include "service/MessageService.hpp"
#include "http/Response.hpp"

namespace chatter {

// 把统一响应体写入一个 HttpSession（协议层渲染，见各 co_ 用法）
static void sendResp(const http::HttpSession::ptr& session, const resp::HttpResponse& response) {
    auto http_resp = session->getResponse();
    http_resp->appendHeader("Content-Type", "application/json");
    http_resp->setBody(response.dump());
    http_resp->setStatus(static_cast<http::StatusCode>(response.getCode()));
}

// 便捷重载：仅凭 code + msg 直接写响应（用于鉴权/协议层错误）
static void sendResp(const http::HttpSession::ptr& session, int code, const std::string& msg) {
    resp::HttpResponse response;
    response.setCode(code).setMsg(msg);
    sendResp(session, response);
}

// 好友相关业务服务：由真实数据库 provider 装配。
// 依赖在这里一次性注入，co_ 只做协议层的事。
static service::FriendService& getFriendService() {
    static service::FriendService instance{
        std::make_shared<dao::FriendDao>(std::make_shared<dao::ProductDbProvider>())
    };
    return instance;
}

// 群组相关业务服务
static service::GroupService& getGroupService() {
    static service::GroupService instance{
        std::make_shared<dao::GroupDao>(std::make_shared<dao::ProductDbProvider>())
    };
    return instance;
}

// 消息查询业务服务
static service::MessageService& getMessageService() {
    static service::MessageService instance{
        std::make_shared<dao::MessageDao>(std::make_shared<dao::ProductDbProvider>())
    };
    return instance;
}

// 从 JSON body 中读取字段为字符串: 兼容 "friendId": "17"(字符串) 与 "friendId": 17(数字)
static std::string jsonFieldToString(const nlohmann::json& body, const std::string& key) {
    if (!body.contains(key)) {
        return "";
    }
    const auto& val = body[key];
    if (val.is_string()) {
        return val.get<std::string>();
    }
    if (val.is_number_integer()) {
        return std::to_string(val.get<long long>());
    }
    if (val.is_number_unsigned()) {
        return std::to_string(val.get<unsigned long long>());
    }
    if (val.is_number_float()) {
        return std::to_string(val.get<double>());
    }
    return "";
}

static Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");
int WsMessage::load(const nlohmann::json& json) {
    int status = -1;
    try {
        if (json.contains("code") && json["code"].is_number_integer()) {
            m_code = static_cast<http::StatusCode>(json["code"].get<int>());
        }
        if (json.contains("from")) {
            if (json["from"].is_number_integer()) {
                m_from = json["from"].get<JettyCat::chat::userId>();
            } else if (json["from"].is_string()) {
                m_from = std::stoll(json["from"].get<std::string>());
            }
        }
        if (json.contains("to")) {
            if (json["to"].is_number_integer()) {
                m_to = json["to"].get<JettyCat::chat::userId>();
            } else if (json["to"].is_string()) {
                m_to = std::stoll(json["to"].get<std::string>());
            }
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
    json["from"] = std::to_string(m_from); // user_id 为 snowflake 大整数, 以字符串传输避免精度丢失
    json["to"] = std::to_string(m_to);
    return json.dump();
}


void registeUrl(const http::HttpServer::ptr& server, const websocket::WsServer::ptr& ws_server) {
    server->GET("/chat/fetch_user_message", co_FetchUserMessage);
    server->GET("/chat/fetch_group_message", co_FetchGroupMessage);
    server->GET("/chat/friend_list", co_GetFriendList);
    server->GET("/chat/group_list", co_GetGroupList);
    server->GET("/chat/user_profile", co_GetUserProfile);
    server->POST("/chat/add_friend", co_AddFriend);
    server->POST("/chat/remove_friend", co_RemoveFriend);
    server->POST("/chat/add_group", co_AddGroup);
    server->POST("/chat/remove_group", co_RemoveGroup);
    ChatWebSocketServer::registeUrl(ws_server);
}


Task<void> co_FetchUserMessage(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchUserMessage, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId jwt_user_id = JWT::parserPayload(jwt).user_id;

    // 协议层职责：取参数
    std::string sender_id_str = req->getParam("senderId");
    std::string receiver_id_str = req->getParam("receiverId");
    std::string offset_str = req->getParam("offset");

    // 业务逻辑交给 service（参数校验 + 越权规则 + 拉取）
    const auto result = co_await getMessageService().fetchUserMessage(
        jwt_user_id, sender_id_str, receiver_id_str, offset_str);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchUserMessage, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_FetchGroupMessage(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchGroupMessage, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：取参数
    std::string group_id_str = req->getParam("groupId");
    std::string offset_str = req->getParam("offset");

    // 业务逻辑交给 service
    const auto result = co_await getMessageService().fetchGroupMessage(group_id_str, offset_str);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchGroupMessage, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_GetFriendList(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetFriendList, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetFriendList, invalid JWT payload, user_id=" << user_id;
        sendResp(session, 403, "FORBIDDEN: Invalid JWT payload");
        co_await session->co_sendResp();
        co_return;
    }

    // 业务逻辑交给 service
    const auto result = co_await getFriendService().getFriendList(user_id);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetFriendList, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_GetGroupList(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetGroupList, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetGroupList, invalid JWT payload, user_id=" << user_id;
        sendResp(session, 403, "FORBIDDEN: Invalid JWT payload");
        co_await session->co_sendResp();
        co_return;
    }

    // 业务逻辑交给 service
    const auto result = co_await getGroupService().getGroupList(user_id);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetGroupList, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_GetUserProfile(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetUserProfile, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId jwt_user_id = JWT::parserPayload(jwt).user_id;
    if (jwt_user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetUserProfile, invalid JWT payload, user_id=" << jwt_user_id;
        sendResp(session, 403, "FORBIDDEN: Invalid JWT payload");
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：取参数
    std::string user_id_str = req->getParam("userId");

    // 业务逻辑交给 service
    const auto result = co_await getFriendService().getPublicProfile(user_id_str);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetUserProfile, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_AddFriend(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, invalid JWT payload, user_id=" << user_id;
        sendResp(session, 403, "FORBIDDEN: Invalid JWT payload");
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：只取参数，合法性校验归 service
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req->getBody());
    } catch (const std::exception&) {
        body = nlohmann::json::object();
    }
    std::string friend_id_str = jsonFieldToString(body, "friendId");

    // 业务逻辑交给 service
    const auto result = co_await getFriendService().addFriend(user_id, friend_id_str);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_RemoveFriend(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveFriend, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveFriend, invalid JWT payload, user_id=" << user_id;
        sendResp(session, 403, "FORBIDDEN: Invalid JWT payload");
        co_await session->co_sendResp();
        co_return;
    }

    // 参数获取
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req->getBody());
    } catch (const std::exception&) {
        body = nlohmann::json::object();
    }
    std::string friend_id_str = jsonFieldToString(body, "friendId");

    // 执行
    const auto result = co_await getFriendService().removeFriend(user_id, friend_id_str);

    // 响应构建
    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveFriend, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_AddGroup(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, invalid JWT payload, user_id=" << user_id;
        sendResp(session, 403, "FORBIDDEN: Invalid JWT payload");
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：只取参数，合法性校验归 service
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req->getBody());
    } catch (const std::exception&) {
        body = nlohmann::json::object();
    }
    std::string group_id_str = jsonFieldToString(body, "groupId");

    // 业务逻辑交给 service
    const auto result = co_await getGroupService().addGroup(user_id, group_id_str);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_RemoveGroup(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveGroup, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveGroup, invalid JWT payload, user_id=" << user_id;
        sendResp(session, 403, "FORBIDDEN: Invalid JWT payload");
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：只取参数，合法性校验归 service
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req->getBody());
    } catch (const std::exception&) {
        body = nlohmann::json::object();
    }
    std::string group_id_str = jsonFieldToString(body, "groupId");

    // 业务逻辑交给 service
    const auto result = co_await getGroupService().removeGroup(user_id, group_id_str);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveGroup, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

}
