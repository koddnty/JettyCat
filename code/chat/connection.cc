#include "init.hpp"
#include "tools.hpp"
#include "connection.hpp"
#include "login/tools.hpp"
#include <basic/config.h>

static m_sylar::Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");

static m_sylar::ConfigVar<std::string>::ptr g_jwtTokenKey = 
    m_sylar::ConfigManager::LookUp("permission_system.key", std::string("jwttoken"), JettyCat_CONFIG_ID, "token key for jwt");



void ChatWebSocketServer::registeUrl(const m_sylar::websocket::WsServer::ptr server) {
    server->registerUrl<ChatHandler>("/chat");
}


// 用户信息
class UserChatInfo : public websocket::SessionInfoBase {
public:
    using ptr = std::shared_ptr<UserChatInfo>;
    UserChatInfo() = default;
    std::string username;
    RolePermissions::Role role {RolePermissions::UNKNOWN};

    int pongLoop {0};           // 记录pong次数,用于减少redis更新,通信次数
};


/**
    @brief WebSocket连接建立时的处理函数, 验证身份并存储会话信息:
                    key: user_{userId}  value: sessionId
*/
m_sylar::Task<void> ChatHandler::co_onOpen(std::shared_ptr<WsSession> session) {
    // 身份验证
    m_sylar::http::Request::ptr request = session->getRequest();
    size_t sessionId = session->getSessionId();

    std::string jwttoken = request->getCookie(g_jwtTokenKey->getValue());

    M_SYLAR_LOG_DEBUG(g_logger) << "WebSocket connection opened, sessionId=" << sessionId << ", jwtToken=" << jwttoken;
    if(JWT::State::SUCCESS != JWT::verifyJWT(jwttoken)) {     // 无法验证jwt，关闭连接
        websocket::Frame frame;
        nlohmann::json j;
        j["status"] = "success";
        j["from"] = "system";
        j["message"] = "Unauthorized";
        frame.setTextPayload(j.dump());
        co_await session->co_sendFrame(frame);
        co_await session->co_close(1008, "Unauthorized");
        co_return;
    }

    JWT::Header header = JWT::parserHeader(jwttoken);
    JWT::Payload payload = JWT::parserPayload(jwttoken);


    // 存储用户id--sessionId映射关系
    M_SYLAR_LOG_DEBUG(g_logger) << "WebSocket connection opened, storing id, sessionId";
    M_SYLAR_LOG_DEBUG(g_logger) << "WebSocket connection opened, storing id, sessionId, user=" << payload.username << ", sessionId=" << sessionId;

    std::string cmd = "SADD " + chatWebsocket::formatUserName(payload.username) + " " + std::to_string(sessionId);
    m_sylar::RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);

    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to store user-session mapping in Redis for user: " << payload.username;
        co_await session->co_close(1011, "Internal Server Error");
        co_return;
    }

    // 添加超时
    cmd = "Expire " + chatWebsocket::formatUserName(payload.username) + " 36000";
    reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "Failed to set expiration for user-session mapping in Redis for user: " << payload.username;
        co_await session->co_close(1011, "Internal Server Error");
        cmd = "DEL " + chatWebsocket::formatUserName(payload.username);
        reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
        if(reply->getState() != m_sylar::IOState::SUCCESS) {
            M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << payload.username;
        }
        co_return;
    }

    // 设置会话用户信息
    auto user = std::make_shared<UserChatInfo>();
    user->username = payload.username;
    user->role = payload.role;
    session->setData(user);

    // 连接成功
    // 发送欢迎消息
    M_SYLAR_LOG_DEBUG(g_logger) << "websocket connection successed";
    websocket::Frame wellcome_frame;
    wellcome_frame.setOpcode(websocket_flags::WS_OP_TEXT);
    nlohmann::json j;
    j["code"] = http::StatusCode::ok;
    j["from"] = "system";
    j["to"] = payload.username;
    j["message"] = "wellcome, " + payload.username + "!";
    wellcome_frame.setTextPayload(j.dump());
    co_await session->co_sendFrame(wellcome_frame);
    M_SYLAR_LOG_DEBUG(g_logger) << "co_open connection finished, sessionId=" << sessionId;
    co_return;
}



/**
 *
 * @brief 会话请求和响应
 */
m_sylar::Task<void> ChatHandler::co_onMessage(std::shared_ptr<WsSession> session, const std::string& msg) {
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, msg:" << msg;
    std::string username = std::dynamic_pointer_cast<UserChatInfo>(session->getData())->username;

    // 解析信息
    nlohmann::json message;
    bool status = true;
    try {
        message = nlohmann::json::parse(msg);
        M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, parser msg finished";
    }
    catch (std::exception& e) {
        status = false;
    }

    M_SYLAR_LOG_DEBUG(g_logger) << "message: " << message.dump(4);

    if (!status) {
        websocket::Frame rt_frame;
        nlohmann::json j;
        j["code"] = http::StatusCode::bad_request;
        j["reason"] = "Failed to parser message";
        j["from"] = "system";
        j["to"] = username;
        rt_frame.setTextPayload(j.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    }


    // 数据完整性检查
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, checking data integrity";
    if (message["from"] != username || message["to"].empty()) {
        websocket::Frame rt_frame;
        nlohmann::json j;
        j["code"] = http::StatusCode::bad_request;
        j["reason"] = "Incomplete information";
        j["from"] = "system";
        j["to"] = username;
        rt_frame.setTextPayload(j.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    };


    // 信息发送到目标
    //      查找目标
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, selecting target user sessionId";
    std::string cmd = "SMEMBERS " + chatWebsocket::formatUserName(message["to"]);
    RedisResp::ptr resp = co_await DB::Redis::getInstance()->executeQuery(cmd);
    if(resp->getState() != m_sylar::IOState::SUCCESS) {
        websocket::Frame rt_frame;
        nlohmann::json j;
        j["code"] = http::StatusCode::internal_server_error;
        j["reason"] = "Internal server error";
        j["from"] = "system";
        j["to"] = username;
        rt_frame.setTextPayload(j.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    }
    const std::vector<RedisResp::ptr>& reply = resp->asArray();

    //      信息构建
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, building send message";
    websocket::Frame frame;
    frame.setOpcode(websocket_flags::WS_OP_TEXT);
    nlohmann::json j;
    j["code"] = http::StatusCode::ok;
    j["from"] = username;
    j["message"] = message["message"];
    frame.setTextPayload(j.dump());

    //      消息发送
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, sending message";
    int rt = 0;
    auto ws = websocket::WsServer::getInstance();
    for (auto& it : reply) {
        M_SYLAR_LOG_DEBUG(g_logger) <<"item : " << it->asString() ;
        int sessionId = std::stoi(it->asString()) ;
        rt = co_await ws->getSession(sessionId)->co_sendFrame(frame);
        if (rt < 0) {
            M_SYLAR_LOG_WARN(g_logger) << "Failed to send frame when broadcast message to others.";
        }
    }

    //      消息持久化
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage finished";
    co_return;
}

m_sylar::Task<void> ChatHandler::co_onBinary(std::shared_ptr<WsSession> session, const std::vector<uint8_t>& data) {
    websocket::Frame frame;
    frame.setOpcode(websocket_flags::WS_OP_TEXT);
    nlohmann::json j;
    j["status"] = "bad msg";
    j["from"] = "system";
    j["message"] = "unsopport binary message";
    frame.setTextPayload(j.dump());
    co_await session->co_sendFrame(frame);
    co_return;
}




/**
 * @brief 会话连接管理
 */
m_sylar::Task<void> ChatHandler::co_onPong(std::shared_ptr<WsSession> session, const std::string& reason) {
    // 根据pong响应,延长登陆时间
    const auto user = std::dynamic_pointer_cast<UserChatInfo>(session->getData());
    if (user->pongLoop / 1000) {
        user->pongLoop = 0;
        const std::string cmd = "EXPIRE " + chatWebsocket::formatUserName(user->username) + " " + std::to_string(36000);

        const RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
        if(reply->getState() != m_sylar::IOState::SUCCESS) {
            M_SYLAR_LOG_ERROR(g_logger) << "Failed to store user-session mapping in Redis for user: " << user->username;
            co_await session->co_close(1011, "Internal Server Error");
            co_return;
        }
    }

    user->pongLoop++;
    co_return;
}




/**
 * @brief 会话关闭处理
 */
m_sylar::Task<void> ChatHandler::co_onClose(std::shared_ptr<WsSession> session, int code, const std::string& reason) {
    const size_t sessionId = session->getSessionId();
    const http::Request::ptr request = session->getRequest();
    if(!request) {
        M_SYLAR_LOG_ERROR(g_logger) << "Request is null in onClose, sessionId=" << sessionId;
        co_return;
    }

    const auto user = std::dynamic_pointer_cast<UserChatInfo>(session->getData());

    // 删除用户id--sessionId映射关系
    std::string cmd = "SREM " + chatWebsocket::formatUserName(user->username) + " " + std::to_string(sessionId);
    m_sylar::RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << user->username;
    }
    co_return;
}

m_sylar::Task<void> ChatHandler::co_onBadClose(std::shared_ptr<WsSession> session) {
    size_t sessionId = session->getSessionId();
    http::Request::ptr request = session->getRequest();
    if(!request) {
        M_SYLAR_LOG_ERROR(g_logger) << "Request is null in onClose, sessionId=" << sessionId;
        co_return;
    }

    const auto user = std::dynamic_pointer_cast<UserChatInfo>(session->getData());

    // 删除用户id--sessionId映射关系
    const std::string cmd = "SREM " + chatWebsocket::formatUserName(user->username) + " " + std::to_string(sessionId);
    const RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << user->username;
    }

    co_return;
}

m_sylar::Task<void> ChatHandler::co_onError(std::shared_ptr<WsSession> session, const std::string& error) {
    co_await ChatHandler::co_onBadClose(session);
    M_SYLAR_LOG_ERROR(g_logger) << "Error: " << error;
    co_return;
}







