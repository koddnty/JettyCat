#pragma once
#include "publicHeader.hpp"
#include <nlohmann/json.hpp>
#include "connection.hpp"

class Chatter{
public:
    Chatter() = default;
    ~Chatter() = default;

    // interface
public:
    static void registeUrl(m_sylar::http::HttpServer::ptr server);

    static m_sylar::Task<void> co_connect(m_sylar::http::HttpSession::ptr session);

    static m_sylar::Task<void> coGetChatterList(m_sylar::http::HttpSession::ptr session);
};


