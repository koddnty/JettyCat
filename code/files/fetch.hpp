// fetch.hpp —— 文件获取的协议层
//
// 职责：CORS + JWT 校验 + 取参数 + 渲染响应，业务逻辑交给 FileService。
// 申请的是"读多个可读位置"的 STS 凭证，把当前用户所有可读位置的读权限写入策略。
#pragma once
#include "publicHeader.hpp"

namespace JettyCat::file::fetch {

// 注册 /files/fetch/sts 路由
void registeUrl(m_sylar::http::HttpServer::ptr server);

// 获取 STS 接口处理函数（协议层）
m_sylar::Task<void> coFetchSts(m_sylar::http::HttpSession::ptr session);

} // namespace JettyCat::file::fetch