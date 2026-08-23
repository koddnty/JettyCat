#pragma once
#include "publicHeader.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace JettyCat::upload {

// STS 临时凭证
struct StsCredential {
    std::string accessKeyId;     // 临时 AccessKeyId
    std::string secretAccessKey; // 临时 SecretAccessKey
    std::string sessionToken;    // 临时 SessionToken
    std::string expiration;      // 过期时间(UTC)
};

class MinioSts {
public:
    MinioSts() = default;
    ~MinioSts() = default;

    // 注册 /upload/sts 路由
    static void registeUrl(m_sylar::http::HttpServer::ptr server);

    // 手动构造并发送 SigV4 签名的 STS AssumeRole 请求, 申请临时凭证
    static m_sylar::Task<StsCredential> assumeRole(unsigned int durationSeconds = 3600);

    // 提供给前端的 STS 接口处理函数
    static m_sylar::Task<void> coGetSts(m_sylar::http::HttpSession::ptr session);
};

} // namespace JettyCat::upload