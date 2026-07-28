#include "init.hpp"
#include "tools.hpp"
#include "connection.hpp"
#include "login/tools.hpp"
#include "MessageList.hpp"
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
    explicit UserChatInfo() = default;
    std::string user_name;
    int user_id {};
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

    // 用户其余信息获取
    std::string cmd = "select users.user_id from users where username = '" + payload.user_name + "';";
    auto resp = co_await DB::Mysql::getInstance()->executeQuery(cmd);
    resp->formatDate();
    if (resp->getState() != IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to fetch all users in MySQL query";
    }
    JettyCat::chat::userId user_id = std::stoi((*resp)["user_id"][0]);

    // 存储用户id--sessionId映射关系
    M_SYLAR_LOG_DEBUG(g_logger) << "WebSocket connection opened, storing id, sessionId";
    M_SYLAR_LOG_DEBUG(g_logger) << "WebSocket connection opened, storing id, sessionId, user=" << payload.user_name << ", sessionId=" << sessionId;

    cmd = "SADD " + chatWebsocket::formatUserName(user_id) + " " + std::to_string(sessionId);
    m_sylar::RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);

    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to store user-session mapping in Redis for user: " << payload.user_name;
        // co_await session->co_close(1011, "Internal Server Error");
        co_return;
    }

    // 添加超时
    cmd = "Expire " + chatWebsocket::formatUserName(user_id) + " 36000";
    reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "Failed to set expiration for user-session mapping in Redis for user: " << payload.user_name;
        co_await session->co_close(1011, "Internal Server Error");
        cmd = "DEL " + chatWebsocket::formatUserName(user_id);
        reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
        if(reply->getState() != m_sylar::IOState::SUCCESS) {
            M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << payload.user_name;
        }
        co_return;
    }


    // 设置会话用户信息

    int rt = 0;
    try {
        auto user = std::make_shared<UserChatInfo>();
        user->user_id = user_id;
        user->user_name = payload.user_name;
        user->role = payload.role;
        session->setData(user);
    } catch (std::exception& e) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to transform " << payload.user_name << " to int";
        rt = 1;
    }
    if (rt) {
        co_await session->co_close(1011, "Internal Server Error");
        co_return;
    }



    // 连接成功
    // 发送欢迎消息
    M_SYLAR_LOG_DEBUG(g_logger) << "websocket connection successed";
    websocket::Frame wellcome_frame;
    wellcome_frame.setOpcode(websocket_flags::WS_OP_TEXT);
    nlohmann::json j;
    j["code"] = http::StatusCode::ok;
    j["from"] = "system";
    j["to"] = payload.user_name;
    j["message"] = "wellcome, " + payload.user_name + "!";
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
    JettyCat::chat::userId user_id = std::dynamic_pointer_cast<UserChatInfo>(session->getData())->user_id;

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
        j["to"] = user_id;
        rt_frame.setTextPayload(j.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    }


    // 数据完整性检查
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, checking data integrity";
    if (message["from"] != std::to_string(user_id) || message["to"].empty() || message["type"].empty()) {
        websocket::Frame rt_frame;
        nlohmann::json j;
        j["code"] = http::StatusCode::bad_request;
        j["reason"] = "Incomplete information";
        j["from"] = "system";
        j["to"] = std::to_string(user_id);
        rt_frame.setTextPayload(j.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    };


    // 数据库归档
    JettyCat::chat::MessageList msg_list;
    JettyCat::chat::Message single_msg;
    single_msg.setFrom(user_id).setContent(message["message"]).setType(std::string(message["type"]));
    msg_list.push_back(single_msg);
    if (JettyCat::chat::State::SUCCESS != co_await sendToUser(user_id, msg_list)) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to send message to user";
        session->co_close(1011, "Internal Server Error(failed to send message to user)");
        co_return;
    }


    // 信息发送到目标
    //      查找目标连接id
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, selecting target user sessionId";
    std::string cmd = "SMEMBERS " + chatWebsocket::formatUserName(std::string(message["to"]));
    RedisResp::ptr resp = co_await DB::Redis::getInstance()->executeQuery(cmd);
    if(resp->getState() != m_sylar::IOState::SUCCESS) {
        websocket::Frame rt_frame;
        nlohmann::json j;
        j["code"] = http::StatusCode::internal_server_error;
        j["reason"] = "Internal server error";
        j["from"] = "system";
        j["to"] = user_id;
        rt_frame.setTextPayload(j.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    }
    const std::vector<RedisResp::ptr>& reply = resp->asArray();
    if (reply.empty()) {
        co_return;
    }

    //      信息构建
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, building send message";
    websocket::Frame frame;
    frame.setOpcode(websocket_flags::WS_OP_TEXT);
    nlohmann::json j;
    j["code"] = http::StatusCode::ok;
    j["from"] = user_id;
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
        const std::string cmd = "EXPIRE " + chatWebsocket::formatUserName(user->user_id) + " " + std::to_string(36000);

        const RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
        if(reply->getState() != m_sylar::IOState::SUCCESS) {
            M_SYLAR_LOG_ERROR(g_logger) << "Failed to store user-session mapping in Redis for user: " << user->user_name;
            // co_await session->co_close(1011, "Internal Server Error");
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
    std::string cmd = "SREM " + chatWebsocket::formatUserName(user->user_name) + " " + std::to_string(sessionId);
    m_sylar::RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << user->user_name;
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
    const std::string cmd = "SREM " + chatWebsocket::formatUserName(user->user_name) + " " + std::to_string(sessionId);
    const RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << user->user_name;
    }

    co_return;
}

m_sylar::Task<void> ChatHandler::co_onError(std::shared_ptr<WsSession> session, const std::string& error) {
    co_await ChatHandler::co_onBadClose(session);
    M_SYLAR_LOG_ERROR(g_logger) << "Error: " << error;
    co_return;
}







