// FileService.hpp —— 文件资源的业务逻辑层
//
// 职责：鉴权信息校验 + 资源路径权限规则 + 调 DAO 申请凭证 + 组装响应。
// 不接触 HTTP/WS 协议细节。全部依赖（FileDao）通过构造函数注入。
#pragma once

#include "files/dao/FileDao.hpp"
#include "http/Response.hpp"
#include "login/tools.hpp"

namespace JettyCat::file::service {

class FileService {
public:
    explicit FileService(std::shared_ptr<dao::FileDao> file_dao)
        : m_file_dao(std::move(file_dao)) {}

    /**
     * @brief 获取 STS 临时凭证（精确到当前用户的资源路径 + 读写权限）
     * @param payload        已解析的 JWT payload（含 user_id / role）
     * @param duration_str   前端传入的凭证时长（原始字符串，可选）
     * @param resource_path  前端请求的资源路径前缀（可选）；为空时默认该用户专属目录
     * @param access_str     前端请求的访问模式（可选）；"read"/"write"/"readwrite"，
     *                       默认只读 read（更安全）
     * @return 统一 HTTP 响应
     */
    [[nodiscard]] m_sylar::Task<chatter::resp::HttpResponse> getSts(
        const JWT::Payload& payload,
        const std::string& duration_str,
        const std::string& resource_path,
        const std::string& access_str) const;

private:
    std::shared_ptr<dao::FileDao> m_file_dao;
};

} // namespace JettyCat::file::service