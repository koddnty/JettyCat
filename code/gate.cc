#include <memory>
#include <csignal>
#include <iostream>
#include <basic/log.h>
#include <basic/address.h>
#include <server/http/httpServer.hpp>
#include <basic/config.h>
#include <coroutine/corobase.h>
#include "publicHeader.hpp"
#include "login/register.hpp"
#include <sylar/server/websocket/wsserver.hpp>
#include "chat/connection.hpp"
#include "init.hpp"
#include "chat/chatter.hpp"
#include "files/files.hpp"

static m_sylar::Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");

void urlReg(m_sylar::http::HttpServer::ptr http_server, m_sylar::websocket::WsServer::ptr ws_server){
    Register::registeUrl(http_server);
    chatter::registeUrl(http_server, ws_server);
    JettyCat::file::files::registeUrl(http_server);
}

int main() {
    m_sylar::IOManager iom("jettyCat", 4);
    m_sylar::http::HttpServer::ptr http_server(new m_sylar::http::HttpServer(&iom));
    m_sylar::Address::ptr addr = m_sylar::Address::LookupAnyIPAddress("0.0.0.0");
    std::dynamic_pointer_cast<m_sylar::IPv4Address>(addr)->setPort(8803);

    m_sylar::websocket::WsServer::ptr ws_server = std::make_shared<m_sylar::websocket::WsServer>(http_server);
    
    projectInit();
    http_server->bind(addr, 6);

    // url注册
    // 登陆注册等接口
    urlReg(http_server, ws_server);

    // WebSocket消息路由注册
    ChatWebSocketServer::initWsRoutes();

    http_server->start();

    projectCleanUp(iom, http_server);
    return 0;
}