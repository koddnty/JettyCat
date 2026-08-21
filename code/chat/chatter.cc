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

// 安全取字符串: null/缺省返回 "", 其他类型返回 dump() (避免 get<std::string>() 对 null 抛异常)
static std::string jsonAsString(const nlohmann::json& val) {
    if (val.is_null()) return "";
    if (val.is_string()) return val.get<std::string>();
    return val.dump();
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

std::string WsMessage::dump() const {
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
    server->POST("/chat/agree_friend", co_AgreeFriend);
    server->GET("/chat/friend_requests", co_GetFriendRequests);
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

    // 协议层职责：取参数 (兼容按 user_id 或按账号 username 查询)
    std::string user_key = req->getParam("userId");
    if (user_key.empty()) {
        user_key = req->getParam("username");
    }

    // 业务逻辑交给 service
    const auto result = co_await getFriendService().getPublicProfile(user_key);

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
    std::string username = jsonFieldToString(body, "username");

    // 业务逻辑交给 service
    nlohmann::json data_out;
    const auto result = co_await getFriendService().addFriend(user_id, username, data_out);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, business failed: " << result.getMsg();
    } else {
        // 同步通信：实时通知被申请方有一条待处理的好友申请
        const nlohmann::json& requester = data_out["requester"];
        const std::string req_nick = jsonAsString(requester["nickname"]);
        const std::string req_user = jsonAsString(requester["username"]);
        nlohmann::json content;
        content["msg"] = (req_nick.empty() ? req_user : req_nick) + " 请求添加你为好友";
        content["requester_id"]       = jsonAsString(requester["user_id"]);
        content["requester_username"] = req_user;
        content["requester_nickname"] = req_nick;
        content["requester_avatar_url"] = jsonAsString(requester["avatar_url"]);

        chatter::WsMessage push_msg;
        push_msg.setStatusCode(http::StatusCode::ok)
                .setType("friend_request")
                .setFrom(user_id)   // 发起方
                .setTo(std::stoll(data_out["friend_id"].get<std::string>()))  // 被申请方
                .setContent(content.dump());
        co_await chatter::pushToUserSessions(std::stoll(data_out["friend_id"].get<std::string>()), push_msg);
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_AgreeFriend(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AgreeFriend, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AgreeFriend, invalid JWT payload, user_id=" << user_id;
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
    std::string username = jsonFieldToString(body, "username");
    if (username.empty()) {
        username = jsonFieldToString(body, "friendId");
    }

    // 执行
    nlohmann::json data_out;
    const auto result = co_await getFriendService().agreeFriend(user_id, username, data_out);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AgreeFriend, business failed: " << result.getMsg();
    } else {
        // 同步通信：实时通知申请方其好友申请已被同意
        const nlohmann::json& friend_info = data_out["friend"];
        const std::string f_nick = jsonAsString(friend_info["nickname"]);
        const std::string f_user = jsonAsString(friend_info["username"]);
        nlohmann::json content;
        content["msg"] = (f_nick.empty() ? f_user : f_nick) + " 同意了你的好友申请";
        content["friend_id"]        = jsonAsString(friend_info["user_id"]);
        content["friend_username"]  = f_user;
        content["friend_nickname"]  = f_nick;
        content["friend_avatar_url"] = jsonAsString(friend_info["avatar_url"]);

        chatter::WsMessage push_msg;
        push_msg.setStatusCode(http::StatusCode::ok)
                .setType("friend_agree")
                .setFrom(user_id)   // 同意方(当前用户)
                .setTo(std::stoll(content["friend_id"].get<std::string>()))
                .setContent(content.dump());
        co_await chatter::pushToUserSessions(std::stoll(content["friend_id"].get<std::string>()), push_msg);
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_GetFriendRequests(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetFriendRequests, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetFriendRequests, invalid JWT payload, user_id=" << user_id;
        sendResp(session, 403, "FORBIDDEN: Invalid JWT payload");
        co_await session->co_sendResp();
        co_return;
    }

    // 业务逻辑交给 service
    const auto result = co_await getFriendService().getFriendRequests(user_id);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetFriendRequests, business failed: " << result.getMsg();
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
    std::string username = jsonFieldToString(body, "username");
    if (username.empty()) {
        username = jsonFieldToString(body, "friendId");
    }

    // 执行
    const auto result = co_await getFriendService().removeFriend(user_id, username);

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
