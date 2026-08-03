#include "WsMessageRouter.hpp"

namespace chatter {

static m_sylar::Logger::ptr g_router_logger = M_SYLAR_LOG_NAME("jettyCat");

WsMessageRouter& WsMessageRouter::getInstance() {
    static WsMessageRouter instance;
    return instance;
}

void WsMessageRouter::on(const std::string& type, Handler handler) {
    on(type, 0, 0, std::move(handler));
}

void WsMessageRouter::on(const std::string& type, JettyCat::chat::userId from,
                          JettyCat::chat::userId to, Handler handler) {
    RouteKey key{type, from, to};
    if (m_routes.contains(key)) {
        M_SYLAR_LOG_WARN(g_router_logger)
            << "WsMessageRouter: overwriting route for type=" << type
            << " from=" << from << " to=" << to;
    }
    m_routes[key] = std::move(handler);
    M_SYLAR_LOG_DEBUG(g_router_logger)
        << "WsMessageRouter: registered route type=" << type
        << " from=" << from << " to=" << to;
}

m_sylar::Task<bool> WsMessageRouter::dispatch(std::shared_ptr<WsSession> session, const WsMessage& msg) {
    const std::string& type = msg.getType();
    const JettyCat::chat::userId from = msg.getFrom();
    const JettyCat::chat::userId to   = msg.getTo();

    // 优先级1: 精确匹配 (type, from, to)
    RouteKey exact_key{type, from, to};
    auto it = m_routes.find(exact_key);
    if (it != m_routes.end()) {
        M_SYLAR_LOG_DEBUG(g_router_logger)
            << "WsMessageRouter: exact match type=" << type
            << " from=" << from << " to=" << to;
        co_await it->second(session, msg);
        co_return true;
    }

    // 优先级2: type-only通配 (type, 0, 0)
    RouteKey wildcard_key{type, 0, 0};
    it = m_routes.find(wildcard_key);
    if (it != m_routes.end()) {
        M_SYLAR_LOG_DEBUG(g_router_logger)
            << "WsMessageRouter: wildcard match type=" << type;
        co_await it->second(session, msg);
        co_return true;
    }

    M_SYLAR_LOG_WARN(g_router_logger)
        << "WsMessageRouter: no handler for type=" << type
        << " from=" << from << " to=" << to;
    co_return false;
}

} // namespace chatter
