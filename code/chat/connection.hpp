#pragma once
#include "publicHeader.hpp"
#include <sylar/server/websocket/wsserver.hpp>



class ChatHandler : public m_sylar::websocket::WsHandler {
public:
    using WsSession = m_sylar::websocket::WsSession;
    static m_sylar::Task<void> co_onOpen(std::shared_ptr<WsSession> session);                                            // 连接建立
    static m_sylar::Task<void> co_onMessage(std::shared_ptr<WsSession> session, const std::string& msg);                 // 文本消息
    static m_sylar::Task<void> co_onBinary(std::shared_ptr<WsSession> session, const std::vector<uint8_t>& data);        // 二进制消息
    static m_sylar::Task<void> co_onPong(std::shared_ptr<WsSession> session, const std::string& reason);                 // 心跳响应
    static m_sylar::Task<void> co_onClose(std::shared_ptr<WsSession> session, int code, const std::string& reason);      // 连接关闭
    static m_sylar::Task<void> co_onBadClose(std::shared_ptr<WsSession> session);                                        // 连接错误导致的关闭
    static m_sylar::Task<void> co_onError(std::shared_ptr<WsSession> session, const std::string& error);                 // 连接错误



};



class ChatWebSocketServer {
public:
    ChatWebSocketServer() = default;
    ~ChatWebSocketServer() = default;

    // interface
public:
    static void registeUrl(m_sylar::websocket::WsServer::ptr server);

    /// 注册所有WsMessage路由 (在registeUrl之后调用)
    static void initWsRoutes();

};