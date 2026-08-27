#pragma once
#include "publicHeader.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace JettyCat::file {

class files {
public:
    files() = default;
    ~files() = default;

    // 注册 /files/sts 路由
    static void registeUrl(m_sylar::http::HttpServer::ptr server);

    // 提供给前端的 STS 接口处理函数（协议层）
    static m_sylar::Task<void> coGetSts(m_sylar::http::HttpSession::ptr session);
};

} // namespace JettyCat::file