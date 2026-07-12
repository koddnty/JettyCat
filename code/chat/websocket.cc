#include "websocket.hpp"
#include "login/tools.hpp"
#include <basic/config.h>

static m_sylar::Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");

static m_sylar::ConfigVar<std::string>::ptr g_jwtTokenKey = 
    m_sylar::ConfigManager::LookUp("permission_system.key", std::string("jwttoken"), JettyCat_CONFIG_ID, "token key for jwt");



void ChatWebSocketServer::registeUrl(m_sylar::websocket::WsServer::ptr server) {
    server->registerUrl<ChatHandler>("/chat");
}



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
    if(!JWT::verifyJWT(jwttoken)) {     // 无法验证jwt，关闭连接
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

    std::string cmd = "SADD " + getInstanceId() + "_user_" + payload.username + " " + std::to_string(sessionId);
    m_sylar::RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);

    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to store user-session mapping in Redis for user: " << payload.username;
        co_await session->co_close(1011, "Internal Server Error");
        co_return;
    }

    // 添加超时
    cmd = "Expire " + getInstanceId() + "_user_" + payload.username + " 36000";
    reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "Failed to set expiration for user-session mapping in Redis for user: " << payload.username;
        co_await session->co_close(1011, "Internal Server Error");
        cmd = "DEL " + getInstanceId() + "_user_" + payload.username;
        reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
        if(reply->getState() != m_sylar::IOState::SUCCESS) {
            M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << payload.username;
        }
        co_return;
    }


    // 连接成功
    // 发送欢迎消息
    M_SYLAR_LOG_DEBUG(g_logger) << "websocket connection successed";
    websocket::Frame wellcome_frame;
    wellcome_frame.setOpcode(websocket_flags::WS_OP_TEXT);
    nlohmann::json j;
    j["status"] = "success";
    j["from"] = "system";
    j["message"] = "wellcome, " + payload.username + "!";
    wellcome_frame.setTextPayload(j.dump());
    co_await session->co_sendFrame(wellcome_frame);
    M_SYLAR_LOG_DEBUG(g_logger) << "co_open connection finished, sessionId=" << sessionId;
    co_return;
}


m_sylar::Task<void> ChatHandler::co_onMessage(std::shared_ptr<WsSession> session, const std::string& msg) {
    websocket::Frame frame;
    frame.setOpcode(websocket_flags::WS_OP_TEXT);
    frame.setTextPayload(msg + "吗?, 你说的对.");
    co_await session->co_sendFrame(frame);
    co_return;
}


m_sylar::Task<void> ChatHandler::co_onBinary(std::shared_ptr<WsSession> session, const std::vector<uint8_t>& data) {
    co_return;
}


m_sylar::Task<void> ChatHandler::co_onClose(std::shared_ptr<WsSession> session, int code, const std::string& reason) {
    size_t sessionId = session->getSessionId();
    http::Request::ptr request = session->getRequest();
    if(!request) {
        M_SYLAR_LOG_ERROR(g_logger) << "Request is null in onClose, sessionId=" << sessionId;
        co_return;
    }


    std::string jwttoken = request->getCookie(g_jwtTokenKey->getValue());
    JWT::Payload payload = JWT::parserPayload(jwttoken);

    // 删除用户id--sessionId映射关系
    std::string cmd = "SREM " + getInstanceId() + "_user_" + payload.username + " " + std::to_string(sessionId);
    m_sylar::RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << payload.username;
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

    std::string jwttoken = request->getCookie(g_jwtTokenKey->getValue());
    JWT::Payload payload = JWT::parserPayload(jwttoken);

    // 删除用户id--sessionId映射关系
    std::string cmd = "SREM " + getInstanceId() + "_user_" + payload.username + " " + std::to_string(sessionId);
    m_sylar::RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << payload.username;
    }

    co_return;
}


m_sylar::Task<void> ChatHandler::co_onError(std::shared_ptr<WsSession> session, const std::string& error) {
    co_return;
}







