#include "FileService.hpp"

namespace JettyCat::file::service {

namespace {

// 访问模式字符串 -> 允许的 S3 动作集合
bool accessToActions(const std::string& access_str, std::vector<policy::Action>& out) {
    out.clear();
    if (access_str == "write") {
        out = {policy::Action::PutObject, policy::Action::DeleteObject};
    } else if (access_str == "readwrite" || access_str == "read_write" || access_str == "read-write") {
        out = {policy::Action::GetObject, policy::Action::PutObject,
               policy::Action::DeleteObject, policy::Action::ListBucket};
    } else if (access_str.empty() || access_str == "read") {
        out = {policy::Action::GetObject, policy::Action::ListBucket};
    } else {
        return false;
    }
    return true;
}

} // namespace

[[nodiscard]] m_sylar::Task<chatter::resp::HttpResponse> FileService::getSts(
    const JWT::Payload& payload,
    const std::string& duration_str,
    const std::string& resource_path,
    const std::string& access_str) const {
    chatter::resp::HttpResponse http_response;

    // ---------- 访问模式解析（读写权限控制） ----------
    // 默认只读，避免前端默认拿到写权限
    std::vector<policy::Action> actions;
    if (!accessToActions(access_str, actions)) {
        http_response.setCode(400).setMsg(
            "BAD_REQUEST: 'access' must be one of read / write / readwrite");
        co_return http_response;
    }

    // ---------- 资源路径权限规则 ----------
    // 默认资源路径为该用户的专属目录: user/<user_id>/
    // 规则（精确到资源路径）：
    //   - 写(WRITE / READ_WRITE)：仅允许写自己的专属目录 user/<user_id>/，防止越权写他人文件
    //   - 读(READ)：允许读任意 user/* 路径（用于查看好友/群分享的图片）
    //                并兼容历史 uploads/* 前缀（重构前的旧对象 key）
    const std::string user_prefix = "user/" + std::to_string(payload.user_id) + "/";
    const bool is_write = (access_str == "write" || access_str == "readwrite" ||
                           access_str == "read_write" || access_str == "read-write");
    const std::string shared_prefix = "user/";   // 只读允许的共享空间前缀
    const std::string legacy_prefix = "uploads/"; // 兼容旧对象 key

    std::string effective_path;
    if (resource_path.empty()) {
        effective_path = user_prefix;
    } else {
        // 规范化: 去掉开头多余的 '/'，保证前缀比较稳定
        std::string p = resource_path;
        if (!p.empty() && p.front() == '/') p.erase(p.begin());
        // 精确权限控制：
        bool allowed = is_write
                           ? (p.rfind(user_prefix, 0) == 0)
                           : (p.rfind(shared_prefix, 0) == 0 || p.rfind(legacy_prefix, 0) == 0);
        if (!allowed) {
            http_response.setCode(403).setMsg(
                "FORBIDDEN: resource path is outside your access scope");
            co_return http_response;
        }
        effective_path = p;
    }

    // ---------- 参数解析 ----------
    unsigned int duration = 3600;
    if (!duration_str.empty()) {
        try {
            duration = static_cast<unsigned int>(std::stoul(duration_str));
        } catch (const std::exception&) {
            duration = 3600;
        }
    }
    if (duration < 60) duration = 60;
    if (duration > 7200) duration = 7200;

    // ---------- 申请凭证 ----------
    const dao::StsCredential cred =
        co_await m_file_dao->assumeRole(duration, effective_path, actions);
    if (cred.accessKeyId.empty() || cred.secretAccessKey.empty() || cred.sessionToken.empty()) {
        http_response.setCode(500).setMsg("Failed to obtain STS credentials");
        co_return http_response;
    }

    nlohmann::json data;
    data["endpoint"]        = m_file_dao->getEndpoint();
    data["bucket"]          = m_file_dao->getBucket();
    data["region"]          = m_file_dao->getRegion();
    data["resourcePath"]    = effective_path;   // 前端直传时应限定在该前缀下
    data["access"]          = access_str.empty() ? "read" : access_str; // 回显实际访问模式
    data["accessKeyId"]     = cred.accessKeyId;
    data["secretAccessKey"] = cred.secretAccessKey;
    data["sessionToken"]    = cred.sessionToken;
    data["expiration"]      = cred.expiration;

    http_response.setCode(200).setMsg("ok").setData(data);
    co_return http_response;
}

} // namespace JettyCat::file::service