#pragma once
#include "publicHeader.hpp"
#include "chatter.hpp"
#include <functional>
#include <unordered_map>

namespace chatter {

/**
 * @brief WebSocket消息路由器 — 根据消息type(+可选from/to)将WsMessage分发到注册的处理协程
 *
 * 路由匹配优先级: (type, from, to)精确匹配 > (type, 0, 0)通配匹配
 *
 * 用法:
 *   auto& router = WsMessageRouter::getInstance();
 *   router.on("private_message", handlePrivateMessage);
 *   router.on("admin_cmd", ADMIN_USER_ID, 0, handleAdminCmd);
 *
 *   然后在co_onMessage中:
 *   bool handled = co_await router.dispatch(session, ws_msg);
 *   if (!handled) { ... 返回错误 ... }
 */
class WsMessageRouter {
public:
    using WsSession = m_sylar::websocket::WsSession;
    using Handler = std::function<m_sylar::Task<void>(std::shared_ptr<WsSession>, const WsMessage&)>;

    /** 路由键: type必填, from=0/to=0表示通配 */
    struct RouteKey {
        std::string type;
        JettyCat::chat::userId from = 0;
        JettyCat::chat::userId to   = 0;

        bool operator==(const RouteKey& other) const {
            return type == other.type && from == other.from && to == other.to;
        }
    };

    struct RouteKeyHash {
        size_t operator()(const RouteKey& k) const {
            size_t h = std::hash<std::string>()(k.type);
            h ^= std::hash<JettyCat::chat::userId>()(k.from) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<JettyCat::chat::userId>()(k.to)   + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    static WsMessageRouter& getInstance();

    /**
     * @brief 注册type-only路由 (from=0, to=0 通配)
     */
    void on(const std::string& type, Handler handler);

    /**
     * @brief 注册精确路由 (from/to非0时精确匹配, 为0时通配)
     */
    void on(const std::string& type, JettyCat::chat::userId from, JettyCat::chat::userId to, Handler handler);

    /**
     * @brief 分发消息到匹配的handler
     * @return true=找到handler并已调用, false=无匹配handler
     */
    m_sylar::Task<bool> dispatch(std::shared_ptr<WsSession> session, const WsMessage& msg);

private:
    WsMessageRouter() = default;
    std::unordered_map<RouteKey, Handler, RouteKeyHash> m_routes;
};

} // namespace chatter
