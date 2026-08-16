#include "chatter.hpp"
#include "login/tools.hpp"
#include "dao/DbProvider.hpp"
#include "dao/FriendDao.hpp"
#include "service/FriendService.hpp"
#include "http/Response.hpp"

namespace chatter {

// 把统一响应体写入一个 HttpSession（协议层渲染，见 co_RemoveFriend 用法）
static void sendResp(const http::HttpSession::ptr& session, const resp::HttpResponse& response) {
    auto http_resp = session->getResponse();
    http_resp->appendHeader("Content-Type", "application/json");
    http_resp->setBody(response.dump());
    http_resp->setStatus(static_cast<http::StatusCode>(response.getCode()));
}

// 好友相关业务服务：由真实数据库 provider 装配。
// 依赖在这里一次性注入，co_RemoveFriend 只做协议层的事。
static service::FriendService& getFriendService() {
    static service::FriendService instance{
        std::make_shared<dao::FriendDao>(std::make_shared<dao::ProductDbProvider>())
    };
    return instance;
}

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
    server->GET("/chat/friend_list", co_GetFriendList);
    server->GET("/chat/group_list", co_GetGroupList);
    server->POST("/chat/add_friend", co_AddFriend);
    server->POST("/chat/remove_friend", co_RemoveFriend);
    server->POST("/chat/add_group", co_AddGroup);
    server->POST("/chat/remove_group", co_RemoveGroup);
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
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchUserMessage, unauthorized: invalid or missing JWT token";
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
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchUserMessage, missing required param: senderId";
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
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchUserMessage, invalid param: " << param_err
                                   << ", senderId=" << sender_id_str << ", offset=" << offset_str;
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
            M_SYLAR_LOG_WARN(g_logger) << "co_FetchUserMessage, forbidden: receiverId with non-self senderId, "
                                       << "senderId=" << sender_id << ", jwt_user_id=" << jwt_user_id;
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
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchUserMessage, invalid receiverId param: " << param_err
                                   << ", receiverId=" << receiver_id_str;
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
    JettyCat::chat::DBState state = co_await JettyCat::chat::fetchFromInbox(sender_id, receiver_id, offset, message_list);

    // 构建响应
    nlohmann::json resp_json;
    if (state == JettyCat::chat::DBState::SUCCESS) {
        resp_json["status"] = "success";
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : message_list) {
            messages.push_back(nlohmann::json::parse(msg.dump()));
        }
        resp_json["messages"] = messages;
        resp_json["count"] = message_list.size();
        resp->setStatus(http::StatusCode::ok);
    } else if (state == JettyCat::chat::DBState::TIMEOUT) {
        M_SYLAR_LOG_ERROR(g_logger) << "co_FetchUserMessage, fetchFromInbox timeout, "
                                    << "sender_id=" << sender_id << ", receiver_id=" << receiver_id
                                    << ", offset=" << offset;
        resp_json["status"] = "error";
        resp_json["error"] = "Database query timeout";
        resp->setStatus(http::StatusCode::internal_server_error);
    } else {
        M_SYLAR_LOG_ERROR(g_logger) << "co_FetchUserMessage, fetchFromInbox failed, "
                                    << "sender_id=" << sender_id << ", receiver_id=" << receiver_id
                                    << ", offset=" << offset;
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
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchGroupMessage, unauthorized: invalid or missing JWT token";
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
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchGroupMessage, missing required param: groupId";
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
        M_SYLAR_LOG_WARN(g_logger) << "co_FetchGroupMessage, invalid param: " << param_err
                                   << ", groupId=" << group_id_str << ", offset=" << offset_str;
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
    JettyCat::chat::DBState state = co_await JettyCat::chat::fetchFromGroup(group_id, offset, message_list);

    // 构建响应
    nlohmann::json resp_json;
    if (state == JettyCat::chat::DBState::SUCCESS) {
        resp_json["status"] = "success";
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : message_list) {
            messages.push_back(nlohmann::json::parse(msg.dump()));
        }
        resp_json["messages"] = messages;
        resp_json["count"] = message_list.size();
        resp->setStatus(http::StatusCode::ok);
    } else if (state == JettyCat::chat::DBState::TIMEOUT) {
        M_SYLAR_LOG_ERROR(g_logger) << "co_FetchGroupMessage, fetchFromGroup timeout, "
                                    << "group_id=" << group_id << ", offset=" << offset;
        resp_json["status"] = "error";
        resp_json["error"] = "Database query timeout";
        resp->setStatus(http::StatusCode::internal_server_error);
    } else {
        M_SYLAR_LOG_ERROR(g_logger) << "co_FetchGroupMessage, fetchFromGroup failed, "
                                    << "group_id=" << group_id << ", offset=" << offset;
        resp_json["status"] = "error";
        resp_json["error"] = "Database query failed";
        resp->setStatus(http::StatusCode::internal_server_error);
    }

    resp->appendHeader("Content-Type", "application/json");
    resp->setBody(resp_json.dump());
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_GetFriendList(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // JWT身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetFriendList, unauthorized: invalid or missing JWT token";
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid or missing JWT token";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 从JWT中解析当前用户身份
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetFriendList, invalid JWT payload, user_id=" << user_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid JWT payload";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 查询好友列表: user_friend 表约束 user_id < friend_id, 因此需要双向查询
    // 方向1: 自己是较小的id, 好友是friend_id; 方向2: 自己是较大的id, 好友是user_id
    const std::string sql =
        "select u.user_id, u.username, u.nickname, u.avatar_url "
        "from user_friend f "
        "join users u on u.user_id = f.friend_id "
        "where f.user_id = ? and f.status = 1 "
        "union "
        "select u.user_id, u.username, u.nickname, u.avatar_url "
        "from user_friend f "
        "join users u on u.user_id = f.user_id "
        "where f.friend_id = ? and f.status = 1";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt<int, STMT_Text<50>, STMT_Text<100>, STMT_Text<500>> stmt {conn_wrap};

    // 执行语句
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, user_id, user_id);
    }
    if (state != IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "co_GetFriendList, execute failed, state=" << (int)state
                                    << ", user_id=" << user_id;
        nlohmann::json j;
        j["status"] = "error";
        j["error"] = state == IOState::TIMEOUT ? "Database query timeout" : "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }

    // 结果获取
    IOState store_state = co_await stmt.co_storeAll();
    if (store_state != IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "co_GetFriendList, storeAll failed, state=" << (int)store_state << ": " << sql;
        nlohmann::json j;
        j["status"] = "error";
        j["error"] = "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }
    IOState fetch_state = co_await stmt.co_fetchAll();
    if (fetch_state != IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "co_GetFriendList, fetchAll failed, state=" << (int)fetch_state << ": " << sql;
        nlohmann::json j;
        j["status"] = "error";
        j["error"] = "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }

    // 构建响应
    nlohmann::json resp_json;
    resp_json["status"] = "success";
    nlohmann::json friends = nlohmann::json::array();
    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        nlohmann::json friend_json;
        friend_json["friend_id"] = std::get<0>(it);
        friend_json["username"] = std::get<1>(it).toString();
        friend_json["nickname"] = std::get<2>(it).toString();
        friend_json["avatar_url"] = std::get<3>(it).toString();
        friends.push_back(friend_json);
    }
    resp_json["friends"] = friends;
    resp_json["count"] = friends.size();
    resp->setStatus(http::StatusCode::ok);
    resp->appendHeader("Content-Type", "application/json");
    resp->setBody(resp_json.dump());
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_GetGroupList(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // JWT身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetGroupList, unauthorized: invalid or missing JWT token";
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid or missing JWT token";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 从JWT中解析当前用户身份
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_GetGroupList, invalid JWT payload, user_id=" << user_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid JWT payload";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 查询当前用户加入的群聊列表
    const std::string sql =
        "select g.group_id, g.group_name, ug.identity, UNIX_TIMESTAMP(ug.join_time) "
        "from user_group ug "
        "join `group` g on ug.group_id = g.group_id "
        "where ug.user_id = ?";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt<int, STMT_Text<256>, STMT_Text<20>, uint64_t> stmt {conn_wrap};

    // 执行语句
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, user_id);
    }
    if (state != IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "co_GetGroupList, execute failed, state=" << (int)state
                                    << ", user_id=" << user_id;
        nlohmann::json j;
        j["status"] = "error";
        j["error"] = state == IOState::TIMEOUT ? "Database query timeout" : "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }

    // 结果获取
    IOState store_state = co_await stmt.co_storeAll();
    if (store_state != IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "co_GetGroupList, storeAll failed, state=" << (int)store_state << ": " << sql;
        nlohmann::json j;
        j["status"] = "error";
        j["error"] = "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }
    IOState fetch_state = co_await stmt.co_fetchAll();
    if (fetch_state != IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "co_GetGroupList, fetchAll failed, state=" << (int)fetch_state << ": " << sql;
        nlohmann::json j;
        j["status"] = "error";
        j["error"] = "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }

    // 构建响应
    nlohmann::json resp_json;
    resp_json["status"] = "success";
    nlohmann::json groups = nlohmann::json::array();
    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        nlohmann::json group_json;
        group_json["group_id"] = std::get<0>(it);
        group_json["group_name"] = std::get<1>(it).toString();
        group_json["identity"] = std::get<2>(it).toString();
        group_json["join_time"] = std::get<3>(it);
        groups.push_back(group_json);
    }
    resp_json["groups"] = groups;
    resp_json["count"] = groups.size();
    resp->setStatus(http::StatusCode::ok);
    resp->appendHeader("Content-Type", "application/json");
    resp->setBody(resp_json.dump());
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_AddFriend(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // JWT身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, unauthorized: invalid or missing JWT token";
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid or missing JWT token";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 从JWT中解析当前用户身份
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, invalid JWT payload, user_id=" << user_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid JWT payload";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 获取请求参数 (body 中的 JSON)
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req->getBody());
    } catch (const std::exception& e) {
        body = nlohmann::json::object();
    }
    std::string friend_id_str = body.value("friendId", "");
    if (friend_id_str.empty()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, missing required param: friendId";
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Missing or invalid 'friendId'";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    JettyCat::chat::userId friend_id = 0;
    bool parse_error = false;
    try {
        friend_id = std::stoi(friend_id_str);
    } catch (const std::exception& e) {
        parse_error = true;
    }

    if (parse_error) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, invalid friendId: " << friend_id_str;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Invalid 'friendId'";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    if (friend_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, invalid friendId: " << friend_id_str;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Invalid 'friendId'";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }
    if (friend_id == user_id) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, cannot add yourself, user_id=" << user_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Cannot add yourself as friend";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    // 校验目标用户存在
    const auto check_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt<int> check_stmt {check_wrap};
    std::string check_sql = "select user_id from users where user_id = ?";
    IOState check_state = IOState::TIMEOUT;
    int check_count = 3;
    while (check_state == IOState::TIMEOUT && check_count--) {
        check_state = co_await check_stmt.co_execute(check_sql, friend_id);
    }
    if (check_state != IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, friend check execute failed, state="
                                   << (int)check_state << ", friend_id=" << friend_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = check_state == IOState::TIMEOUT ? "Database query timeout" : "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }
    IOState store_state = co_await check_stmt.co_storeAll();
    if (store_state != IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, friend check storeAll failed, state="
                                   << (int)store_state << ", friend_id=" << friend_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }
    IOState fetch_state = co_await check_stmt.co_fetchAll();
    if (fetch_state != IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, friend check fetchAll failed, state="
                                   << (int)fetch_state << ", friend_id=" << friend_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }
    if (check_stmt.getResult().getAll().empty()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddFriend, friend does not exist, friend_id=" << friend_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "NOT_FOUND: friendId does not exist";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::not_found);
        co_await session->co_sendResp();
        co_return;
    }

    // user_friend 表 CHECK 约束: user_id < friend_id, 因此排序后写入
    JettyCat::chat::userId min_id = std::min(user_id, friend_id);
    JettyCat::chat::userId max_id = std::max(user_id, friend_id);

    // 添加好友 (已存在则恢复为正常状态)
    const std::string sql =
        "insert into user_friend (user_id, friend_id, status) "
        "values (?, ?, 1) "
        "on duplicate key update status = 1";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt stmt {conn_wrap};
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, min_id, max_id);
    }

    nlohmann::json resp_json;
    if (state == IOState::SUCCESS) {
        resp_json["status"] = "success";
        resp_json["message"] = "Friend added";
        resp->setStatus(http::StatusCode::ok);
    } else {
        M_SYLAR_LOG_ERROR(g_logger) << "co_AddFriend, execute failed, state=" << (int)state
                                    << ", user_id=" << min_id << ", friend_id=" << max_id;
        resp_json["status"] = "error";
        resp_json["error"] = state == IOState::TIMEOUT ? "Database query timeout" : "Database query failed";
        resp->setStatus(http::StatusCode::internal_server_error);
    }
    resp->appendHeader("Content-Type", "application/json");
    resp->setBody(resp_json.dump());
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_RemoveFriend(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveFriend, unauthorized: invalid or missing JWT token";
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid or missing JWT token";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveFriend, invalid JWT payload, user_id=" << user_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid JWT payload";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
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
    std::string friend_id_str = body.value("friendId", "");

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
    http::Response::ptr resp = session->getResponse();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // JWT身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, unauthorized: invalid or missing JWT token";
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid or missing JWT token";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 从JWT中解析当前用户身份
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, invalid JWT payload, user_id=" << user_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid JWT payload";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 获取请求参数 (body 中的 JSON)
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req->getBody());
    } catch (const std::exception& e) {
        body = nlohmann::json::object();
    }
    std::string group_id_str = body.value("groupId", "");
    if (group_id_str.empty()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, missing required param: groupId";
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
    bool parse_error = false;
    try {
        group_id = std::stoi(group_id_str);
    } catch (const std::exception& e) {
        parse_error = true;
    }

    if (parse_error) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, invalid groupId: " << group_id_str;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Invalid 'groupId'";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    if (group_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, invalid groupId: " << group_id_str;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Invalid 'groupId'";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    // 校验目标群聊存在
    const auto check_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt<int> check_stmt {check_wrap};
    std::string check_sql = "select group_id from `group` where group_id = ?";
    IOState check_state = IOState::TIMEOUT;
    int check_count = 3;
    while (check_state == IOState::TIMEOUT && check_count--) {
        check_state = co_await check_stmt.co_execute(check_sql, group_id);
    }
    if (check_state != IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, group check execute failed, state="
                                   << (int)check_state << ", group_id=" << group_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = check_state == IOState::TIMEOUT ? "Database query timeout" : "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }
    IOState store_state = co_await check_stmt.co_storeAll();
    if (store_state != IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, group check storeAll failed, state="
                                   << (int)store_state << ", group_id=" << group_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }
    IOState fetch_state = co_await check_stmt.co_fetchAll();
    if (fetch_state != IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, group check fetchAll failed, state="
                                   << (int)fetch_state << ", group_id=" << group_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Database query failed";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }
    if (check_stmt.getResult().getAll().empty()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_AddGroup, group does not exist, group_id=" << group_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "NOT_FOUND: groupId does not exist";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::not_found);
        co_await session->co_sendResp();
        co_return;
    }

    // 加入群聊 (已存在则刷新加入时间)
    const std::string sql =
        "insert into user_group (user_id, group_id, identity) "
        "values (?, ?, 'member') "
        "on duplicate key update join_time = current_timestamp";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt stmt {conn_wrap};
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, user_id, group_id);
    }

    nlohmann::json resp_json;
    if (state == IOState::SUCCESS) {
        resp_json["status"] = "success";
        resp_json["message"] = "Group joined";
        resp->setStatus(http::StatusCode::ok);
    } else {
        M_SYLAR_LOG_ERROR(g_logger) << "co_AddGroup, execute failed, state=" << (int)state
                                    << ", user_id=" << user_id << ", group_id=" << group_id;
        resp_json["status"] = "error";
        resp_json["error"] = state == IOState::TIMEOUT ? "Database query timeout" : "Database query failed";
        resp->setStatus(http::StatusCode::internal_server_error);
    }
    resp->appendHeader("Content-Type", "application/json");
    resp->setBody(resp_json.dump());
    co_await session->co_sendResp();
    co_return;
}

Task<void> co_RemoveGroup(http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // JWT身份验证
    std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveGroup, unauthorized: invalid or missing JWT token";
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid or missing JWT token";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 从JWT中解析当前用户身份
    JettyCat::chat::userId user_id = JWT::parserPayload(jwt).user_id;
    if (user_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveGroup, invalid JWT payload, user_id=" << user_id;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Invalid JWT payload";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 获取请求参数 (body 中的 JSON)
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req->getBody());
    } catch (const std::exception& e) {
        body = nlohmann::json::object();
    }
    std::string group_id_str = body.value("groupId", "");
    if (group_id_str.empty()) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveGroup, missing required param: groupId";
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
    bool parse_error = false;
    try {
        group_id = std::stoi(group_id_str);
    } catch (const std::exception& e) {
        parse_error = true;
    }

    if (parse_error) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveGroup, invalid groupId: " << group_id_str;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Invalid 'groupId'";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    if (group_id <= 0) {
        M_SYLAR_LOG_WARN(g_logger) << "co_RemoveGroup, invalid groupId: " << group_id_str;
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "BAD_REQUEST: Invalid 'groupId'";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    // 退出群聊
    // 注意: user_group 表无 status 字段, 无法标记删除, 暂以物理删除实现.
    const std::string sql =
        "delete from user_group where user_id = ? and group_id = ?";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt stmt {conn_wrap};
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, user_id, group_id);
    }

    nlohmann::json resp_json;
    if (state == IOState::SUCCESS) {
        resp_json["status"] = "success";
        resp_json["message"] = "Group left";
        resp->setStatus(http::StatusCode::ok);
    } else {
        M_SYLAR_LOG_ERROR(g_logger) << "co_RemoveGroup, execute failed, state=" << (int)state
                                    << ", user_id=" << user_id << ", group_id=" << group_id;
        resp_json["status"] = "error";
        resp_json["error"] = state == IOState::TIMEOUT ? "Database query timeout" : "Database query failed";
        resp->setStatus(http::StatusCode::internal_server_error);
    }
    resp->appendHeader("Content-Type", "application/json");
    resp->setBody(resp_json.dump());
    co_await session->co_sendResp();
    co_return;
}

}
