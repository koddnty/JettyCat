#include "init.hpp"
#include "tools.hpp"
#include "connection.hpp"
#include "chatter.hpp"
#include "login/tools.hpp"
#include "Message.hpp"
#include "WsMessageRouter.hpp"
#include <basic/config.h>


static m_sylar::Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");

static m_sylar::ConfigVar<std::string>::ptr g_jwtTokenKey =
    m_sylar::ConfigManager::LookUp("permission_system.key", std::string("jwttoken"), JettyCat_CONFIG_ID,
                                   "token key for jwt");


void ChatWebSocketServer::registeUrl(const m_sylar::websocket::WsServer::ptr server) {
    server->registerUrl<ChatHandler>("/chat");
}


// 用户信息
class UserChatInfo : public websocket::SessionInfoBase {
public:
    using ptr = std::shared_ptr<UserChatInfo>;
    explicit UserChatInfo() = default;
    std::string user_name;
    int user_id{};
    RolePermissions::Role role{RolePermissions::UNKNOWN};

    int pongLoop{0}; // 记录pong次数,用于减少redis更新,通信次数
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
    if (JWT::State::SUCCESS != JWT::verifyJWT(jwttoken)) {
        // 无法验证jwt，关闭连接
        chatter::WsMessage err_msg;
        err_msg.setStatusCode(http::StatusCode::unauthorized)
               .setFrom(0);
        websocket::Frame frame;
        frame.setOpcode(websocket_flags::WS_OP_TEXT);
        frame.setTextPayload(err_msg.dump());
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
    M_SYLAR_LOG_DEBUG(g_logger) << "WebSocket connection opened, storing id, sessionId, user=" << payload.user_name <<
 ", sessionId=" << sessionId;

    cmd = "SADD " + chatWebsocket::formatUserName(user_id) + " " + std::to_string(sessionId);
    m_sylar::RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);

    if (reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to store user-session mapping in Redis for user: " << payload.user_name;
        co_return;
    }

    // 添加超时
    cmd = "Expire " + chatWebsocket::formatUserName(user_id) + " 36000";
    reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if (reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_WARN(g_logger) << "Failed to set expiration for user-session mapping in Redis for user: " << payload
.user_name;
        co_await session->co_close(1011, "Internal Server Error");
        cmd = "DEL " + chatWebsocket::formatUserName(user_id);
        reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
        if (reply->getState() != m_sylar::IOState::SUCCESS) {
            M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << payload.
user_name;
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
    }
    catch (std::exception& e) {
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
    chatter::WsMessage welcome_msg;
    welcome_msg.setStatusCode(http::StatusCode::ok)
               .setFrom(0)
               .setTo(user_id)
               .setContent("wellcome, " + payload.user_name + "!");
    websocket::Frame wellcome_frame;
    wellcome_frame.setOpcode(websocket_flags::WS_OP_TEXT);
    wellcome_frame.setTextPayload(welcome_msg.dump());
    co_await session->co_sendFrame(wellcome_frame);
    M_SYLAR_LOG_DEBUG(g_logger) << "co_open connection finished, sessionId=" << sessionId;
    co_return;
}


// ========== 消息路由处理函数 ==========

/**
 * @brief 处理 private_message: 发送私聊消息给目标用户
 *
 * 流程: 校验发送者 → 解析内层Message → 数据库持久化 → Redis查找目标session → 转发
 */
static m_sylar::Task<void> handlePrivateMessage(std::shared_ptr<ChatHandler::WsSession> session,
                                                const chatter::WsMessage& ws_msg) {
    JettyCat::chat::userId user_id = std::dynamic_pointer_cast<UserChatInfo>(session->getData())->user_id;

    // 字段提取与校验
    JettyCat::chat::userId sender_id = ws_msg.getFrom();
    JettyCat::chat::userId receiver_id = ws_msg.getTo();
    std::string content = ws_msg.getContent();

    if (sender_id != user_id) {
        M_SYLAR_LOG_WARN(g_logger) << "handlePrivateMessage, sender mismatch, sender=" << sender_id
                                   << " user=" << user_id;
        chatter::WsMessage err_msg;
        err_msg.setStatusCode(http::StatusCode::bad_request, "Sender mismatch")
               .setFrom(0).setTo(0);
        websocket::Frame rt_frame;
        rt_frame.setOpcode(websocket_flags::WS_OP_TEXT);
        rt_frame.setTextPayload(err_msg.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    }

    // 从WsMessage.content反序列化业务消息
    JettyCat::chat::MessageList msg_list;
    JettyCat::chat::Message single_msg;
    if (single_msg.load(content) != 0) {
        M_SYLAR_LOG_WARN(g_logger) << "handlePrivateMessage, failed to parse business message content";
        chatter::WsMessage err_msg;
        err_msg.setStatusCode(http::StatusCode::bad_request, "Failed to parse message content")
               .setFrom(0).setTo(0);
        websocket::Frame rt_frame;
        rt_frame.setOpcode(websocket_flags::WS_OP_TEXT);
        rt_frame.setTextPayload(err_msg.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    }
    single_msg.setFrom(user_id); // 覆盖from为session用户id，确保安全
    msg_list.push_back(single_msg);

    // 数据库持久化
    JettyCat::chat::State db_state = co_await sendToUser(receiver_id, msg_list);
    if (db_state != JettyCat::chat::State::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "handlePrivateMessage, failed to persist message to db";
        chatter::WsMessage err_msg;
        err_msg.setStatusCode(http::StatusCode::internal_server_error, "Failed to persist message")
               .setFrom(0).setTo(0);
        websocket::Frame rt_frame;
        rt_frame.setOpcode(websocket_flags::WS_OP_TEXT);
        rt_frame.setTextPayload(err_msg.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    }

    // 查找目标连接
    M_SYLAR_LOG_DEBUG(g_logger) << "handlePrivateMessage, lookup target sessions, receiver=" << receiver_id;
    std::string cmd = "SMEMBERS " + chatWebsocket::formatUserName(receiver_id);
    RedisResp::ptr resp = co_await DB::Redis::getInstance()->executeQuery(cmd);
    if (resp->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "handlePrivateMessage, redis query failed";
        chatter::WsMessage err_msg;
        err_msg.setStatusCode(http::StatusCode::internal_server_error, "Internal server error")
               .setFrom(0).setTo(0);
        websocket::Frame rt_frame;
        rt_frame.setOpcode(websocket_flags::WS_OP_TEXT);
        rt_frame.setTextPayload(err_msg.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    }
    const std::vector<RedisResp::ptr>& reply = resp->asArray();
    if (reply.empty()) {
        M_SYLAR_LOG_DEBUG(g_logger) << "handlePrivateMessage, target user offline, receiver=" << receiver_id;
        co_return;
    }

    // 构建转发消息
    chatter::WsMessage fwd_msg;
    fwd_msg.setStatusCode(http::StatusCode::ok)
           .setType("private_message")
           .setFrom(user_id)
           .setTo(receiver_id)
           .setContent(single_msg.dump());
    std::string payload = fwd_msg.dump();

    // 转发到所有目标session
    auto ws = websocket::WsServer::getInstance();
    for (auto& it : reply) {
        int session_id = std::stoi(it->asString());
        websocket::Frame frame;
        frame.setOpcode(websocket_flags::WS_OP_TEXT);
        frame.setTextPayload(payload);
        auto target_session = ws->getSession(session_id);
        int rt = co_await target_session->co_sendFrame(frame);
        if (rt < 0) {
            M_SYLAR_LOG_DEBUG(g_logger) << "Failed to send frame to session " << session_id;
        }
    }

    M_SYLAR_LOG_DEBUG(g_logger) << "handlePrivateMessage finished";
    co_return;
}


// ========== 路由注册 ==========
void ChatWebSocketServer::initWsRoutes() {
    auto& router = chatter::WsMessageRouter::getInstance();
    router.on("private_message", handlePrivateMessage);
    M_SYLAR_LOG_INFO(g_logger) << "WsMessage routes registered";
}


// ========== co_on系列回调 ==========
/**
 * @brief WebSocket文本消息入口 — 反序列化后交由路由器分发
 */
m_sylar::Task<void> ChatHandler::co_onMessage(std::shared_ptr<WsSession> session, const std::string& msg) {
    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, msg:" << msg;

    // 反序列化
    chatter::WsMessage ws_msg;
    if (ws_msg.load(msg) != 0 || ws_msg.getState() != chatter::WsMessage::State::NORMAL) {
        M_SYLAR_LOG_WARN(g_logger) << "co_onMessage, failed to parse message";
        chatter::WsMessage err_msg;
        err_msg.setStatusCode(http::StatusCode::bad_request, "Failed to parse message")
               .setFrom(0).setTo(0);
        websocket::Frame rt_frame;
        rt_frame.setOpcode(websocket_flags::WS_OP_TEXT);
        rt_frame.setTextPayload(err_msg.dump());
        co_await session->co_sendFrame(rt_frame);
        co_return;
    }

    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage, parsed ws_msg: " << ws_msg.dump();

    // 路由分发
    if (!co_await chatter::WsMessageRouter::getInstance().dispatch(session, ws_msg)) {
        M_SYLAR_LOG_WARN(g_logger) << "co_onMessage, no handler for type=" << ws_msg.getType();
        chatter::WsMessage err_msg;
        err_msg.setStatusCode(http::StatusCode::not_found, "Unknown message type: " + ws_msg.getType())
               .setFrom(0).setTo(0);
        websocket::Frame rt_frame;
        rt_frame.setOpcode(websocket_flags::WS_OP_TEXT);
        rt_frame.setTextPayload(err_msg.dump());
        co_await session->co_sendFrame(rt_frame);
    }

    M_SYLAR_LOG_DEBUG(g_logger) << "co_onMessage finished";
    co_return;
}


m_sylar::Task<void> ChatHandler::co_onBinary(std::shared_ptr<WsSession> session, const std::vector<uint8_t>& data) {
    chatter::WsMessage err_msg;
    err_msg.setStatusCode(http::StatusCode::bad_request, "unsupported binary message")
           .setFrom(0);
    websocket::Frame frame;
    frame.setOpcode(websocket_flags::WS_OP_TEXT);
    frame.setTextPayload(err_msg.dump());
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
        if (reply->getState() != m_sylar::IOState::SUCCESS) {
            M_SYLAR_LOG_ERROR(g_logger) << "Failed to store user-session mapping in Redis for user: " << user->
user_name;
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
    if (!request) {
        M_SYLAR_LOG_ERROR(g_logger) << "Request is null in onClose, sessionId=" << sessionId;
        co_return;
    }

    // 删除用户id--sessionId映射关系
    auto data = session->getData();
    if (data) {
        const auto user = std::dynamic_pointer_cast<UserChatInfo>(data);
        const std::string cmd = "SREM " + chatWebsocket::formatUserName(user->user_id) + " " +
            std::to_string(sessionId);
        m_sylar::RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
        if (reply->getState() != m_sylar::IOState::SUCCESS) {
            M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << user->
user_name;
        }
    }


    co_return;
}

m_sylar::Task<void> ChatHandler::co_onBadClose(std::shared_ptr<WsSession> session) {
    size_t sessionId = session->getSessionId();
    http::Request::ptr request = session->getRequest();
    if (!request) {
        M_SYLAR_LOG_ERROR(g_logger) << "Request is null in onClose, sessionId=" << sessionId;
        co_return;
    }

    const auto user = std::dynamic_pointer_cast<UserChatInfo>(session->getData());

    // 删除用户id--sessionId映射关系
    const std::string cmd = "SREM " + chatWebsocket::formatUserName(user->user_id) + " " + std::to_string(sessionId);
    const RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if (reply->getState() != m_sylar::IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(g_logger) << "Failed to delete user-session mapping in Redis for user: " << user->user_name;
    }

    co_return;
}

m_sylar::Task<void> ChatHandler::co_onError(std::shared_ptr<WsSession> session, const std::string& error) {
    co_await ChatHandler::co_onBadClose(session);
    M_SYLAR_LOG_ERROR(g_logger) << "Error: " << error;
    co_return;
}
