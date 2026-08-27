#pragma once
#include "publicHeader.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace JettyCat::file {

class files {
public:
    files() = default;
    ~files() = default;

    // 注册文件相关路由（上传/获取 STS）
    static void registeUrl(m_sylar::http::HttpServer::ptr server);
};

} // namespace JettyCat::file