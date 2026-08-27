// upload.hpp —— 文件上传的协议层
//
// 职责：CORS + JWT 校验 + 取参数 + 渲染响应，业务逻辑交给 FileService。
// 申请的是"写单个对象"的 STS 凭证，对象 key 由消息产生地决定：
//   - 私聊：<接收者 user_snow_id>/<文件hash>
//   - 群聊：<群聊 group_snow_id>/<文件hash>
#pragma once
#include "publicHeader.hpp"

namespace JettyCat::file::upload {

// 注册 /files/upload/sts 路由
void registeUrl(m_sylar::http::HttpServer::ptr server);

// 上传 STS 接口处理函数（协议层）
m_sylar::Task<void> coUploadSts(m_sylar::http::HttpSession::ptr session);

} // namespace JettyCat::file::upload