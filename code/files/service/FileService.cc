#include "FileService.hpp"

static m_sylar::Logger::ptr j_logger = M_SYLAR_LOG_NAME("jettyCat");

namespace JettyCat::file::service {

namespace {

// 配置：获取凭证默认时长（秒），默认 3 小时
m_sylar::ConfigVar<unsigned int>::ptr g_fetch_sts_duration =
    m_sylar::ConfigManager::LookUp<unsigned int>("files.fetch.stsDurationSeconds", 10800,
                                                 JettyCat_CONFIG_ID, "fetch sts default duration");

// 解析非负整数，失败返回默认值
unsigned int parseDuration(const std::string& duration_str, unsigned int def) {
    if (duration_str.empty()) return def;
    try {
        return static_cast<unsigned int>(std::stoul(duration_str));
    } catch (const std::exception&) {
        return def;
    }
}

} // namespace

[[nodiscard]] m_sylar::Task<chatter::resp::HttpResponse> FileService::getUploadSts(
    const JWT::Payload& payload,
    const std::string& target_kind,
    const std::string& target_id_str,
    const std::string& file_hash,
    const std::string& duration_str) const {
    chatter::resp::HttpResponse http_response;

    // ---------- 参数校验 ----------
    if (target_kind != "user" && target_kind != "group") {
        http_response.setCode(400).setMsg("BAD_REQUEST: 'kind' must be user or group");
        co_return http_response;
    }
    int64_t target_id = 0;
    try {
        target_id = std::stoll(target_id_str);
    } catch (const std::exception&) {
        http_response.setCode(400).setMsg("BAD_REQUEST: invalid 'target' snow id");
        co_return http_response;
    }
    if (target_id <= 0) {
        http_response.setCode(400).setMsg("BAD_REQUEST: invalid 'target' snow id");
        co_return http_response;
    }
    if (file_hash.empty() || file_hash.size() > 128) {
        http_response.setCode(400).setMsg("BAD_REQUEST: invalid 'hash'");
        co_return http_response;
    }

    // ---------- 越权校验：目标必须是自己的好友 / 已加入的群 ----------
    if (target_kind == "user") {
        int status = 0;
        bool exists = false;
        const auto st = co_await m_friend_dao->getFriendStatus(
            payload.user_id, target_id, status, exists);
        if (st != JettyCat::chat::DBState::SUCCESS || !exists ||
            status != static_cast<int>(chatter::dao::FriendStatus::NORMAL)) {
            http_response.setCode(403).setMsg(
                "FORBIDDEN: target is not your friend");
            co_return http_response;
        }
    } else { // group
        std::vector<JettyCat::chat::groupId> joined;
        const auto st = co_await m_group_dao->listJoinedGroupIds(payload.user_id, joined);
        if (st != JettyCat::chat::DBState::SUCCESS ||
            std::find(joined.begin(), joined.end(), target_id) == joined.end()) {
            http_response.setCode(403).setMsg(
                "FORBIDDEN: target group is not joined");
            co_return http_response;
        }
    }

    // ---------- 对象 key：<目标snow_id>/<文件hash> ----------
    const std::string object_key = std::to_string(target_id) + "/" + file_hash;

    // ---------- 凭证时长：默认 900s(15min) ----------
    // MinIO STS 的 DurationSeconds 最小值为 900 秒，低于此值返回 invalid token expiry
    unsigned int duration = parseDuration(duration_str, 900);
    if (duration < 900) duration = 900;
    if (duration > 3600) duration = 3600;

    // ---------- 写凭证：只允许 PutObject 到这一个对象 ----------
    const policy::Policy policyDoc = dao::FileDao::buildObjectPolicy(
        m_file_dao->getBucket(), object_key, {policy::Action::PutObject});
    const dao::StsCredential cred = co_await m_file_dao->assumeRoleWithPolicy(duration, policyDoc);
    if (cred.accessKeyId.empty() || cred.secretAccessKey.empty() || cred.sessionToken.empty()) {
        http_response.setCode(500).setMsg("Failed to obtain STS credentials");
        co_return http_response;
    }

    nlohmann::json data;
    data["endpoint"]        = m_file_dao->getEndpoint();
    data["bucket"]          = m_file_dao->getBucket();
    data["region"]          = m_file_dao->getRegion();
    data["objectKey"]       = object_key;       // 前端直传应 PUT 到此对象
    data["access"]          = "write";
    data["accessKeyId"]     = cred.accessKeyId;
    data["secretAccessKey"] = cred.secretAccessKey;
    data["sessionToken"]    = cred.sessionToken;
    data["expiration"]      = cred.expiration;

    http_response.setCode(200).setMsg("ok").setData(data);
    co_return http_response;
}

[[nodiscard]] m_sylar::Task<chatter::resp::HttpResponse> FileService::getFetchSts(
    const JWT::Payload& payload,
    const std::string& duration_str) const {
    chatter::resp::HttpResponse http_response;

    // ---------- 收集当前用户所有可读位置 ----------
    // 1) 自己的目录：他人发给我的图片 <自己>/<hash>
    // 2) 每个好友的目录：我发给好友的图片 <好友>/<hash>
    // 3) 每个已加入群的目录：群聊图片 <群>/<hash>
    // 4) 兼容历史对象 key：重构前图片存于 user/<id>/uploads/...，只读放开该前缀
    std::vector<std::string> readable_paths;
    readable_paths.push_back(std::to_string(payload.user_id) + "/");
    readable_paths.push_back("user/");

    nlohmann::json friends;
    const auto fst = co_await m_friend_dao->listFriends(payload.user_id, friends);
    if (fst != JettyCat::chat::DBState::SUCCESS) {
        http_response.setCode(500).setMsg("Failed to load friend list");
        co_return http_response;
    }
    for (const auto& f : friends) {
        const std::string fid = f.value("friend_id", "");
        if (!fid.empty()) readable_paths.push_back(fid + "/");
    }

    std::vector<JettyCat::chat::groupId> groups;
    const auto gst = co_await m_group_dao->listJoinedGroupIds(payload.user_id, groups);
    if (gst != JettyCat::chat::DBState::SUCCESS) {
        http_response.setCode(500).setMsg("Failed to load group list");
        co_return http_response;
    }
    for (const auto& gid : groups) {
        readable_paths.push_back(std::to_string(gid) + "/");
    }

    // ---------- 凭证时长：默认 3 小时，可配置 ----------
    unsigned int duration = parseDuration(duration_str, g_fetch_sts_duration->getValue());
    if (duration < 300) duration = 300;
    if (duration > 86400) duration = 86400;

    // ---------- 读凭证：对全部可读位置授予读权限 ----------
    const policy::Policy policyDoc = dao::FileDao::buildPrefixesPolicy(
        m_file_dao->getBucket(), readable_paths,
        {policy::Action::GetObject, policy::Action::ListBucket});
    const dao::StsCredential cred = co_await m_file_dao->assumeRoleWithPolicy(duration, policyDoc);
    if (cred.accessKeyId.empty() || cred.secretAccessKey.empty() || cred.sessionToken.empty()) {
        http_response.setCode(500).setMsg("Failed to obtain STS credentials");
        co_return http_response;
    }

    nlohmann::json data;
    data["endpoint"]        = m_file_dao->getEndpoint();
    data["bucket"]          = m_file_dao->getBucket();
    data["region"]          = m_file_dao->getRegion();
    data["readablePaths"]   = readable_paths;    // 该凭证可读的全部对象前缀
    data["access"]          = "read";
    data["accessKeyId"]     = cred.accessKeyId;
    data["secretAccessKey"] = cred.secretAccessKey;
    data["sessionToken"]    = cred.sessionToken;
    data["expiration"]      = cred.expiration;

    http_response.setCode(200).setMsg("ok").setData(data);
    co_return http_response;
}

} // namespace JettyCat::file::service