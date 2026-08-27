// FileDao.hpp —— 文件资源的数据访问层
//
// 职责：面向对象存储(MinIO)的 STS 临时凭证申请与资源路径级策略构建。
// 只负责"取凭证/拼策略"，不关心 HTTP/JSON 响应。配置经 ConfigManager 读取。
#pragma once

#include "publicHeader.hpp"
#include "files/policy/Policy.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace JettyCat::file::dao {

// STS 临时凭证
struct StsCredential {
    std::string accessKeyId;     // 临时 AccessKeyId
    std::string secretAccessKey; // 临时 SecretAccessKey
    std::string sessionToken;    // 临时 SessionToken
    std::string expiration;      // 过期时间(UTC)
};

/**
 * @brief 对象存储数据访问层
 *
 * 与其它 DAO 一样使用协程 + 依赖注入，此处无数据库连接，
 * 通过 libcurl 直接向 MinIO 的 STS 服务发起 SigV4 签名请求。
 * 权限通过类型安全的 policy::Policy 描述。
 */
class FileDao {
public:
    FileDao() = default;
    ~FileDao() = default;

    /**
     * @brief 申请 STS 临时凭证（精确到资源路径 + 访问模式）
     * @param durationSeconds  凭证有效时长（秒）
     * @param resourcePath     资源路径前缀（对象 key 前缀），为空则不限；
     *                         形如 "17/" —— 凭证只能访问该前缀下的对象
     * @param actions          允许的 S3 动作集合（精确到读写）
     * @return 临时凭证；失败时各字段为空串
     */
    [[nodiscard]] m_sylar::Task<StsCredential> assumeRole(
        unsigned int durationSeconds = 3600,
        const std::string& resourcePath = "",
        const std::vector<policy::Action>& actions = {}) const;

    /**
     * @brief 申请 STS 临时凭证（使用完整的自定义策略，支持任意配置）
     * @param durationSeconds  凭证有效时长（秒）
     * @param policyDoc        类型安全的策略对象
     * @return 临时凭证；失败时各字段为空串
     */
    [[nodiscard]] m_sylar::Task<StsCredential> assumeRoleWithPolicy(
        unsigned int durationSeconds, const policy::Policy& policyDoc) const;

    /**
     * @brief 便捷构造"读写路径策略"：把凭证限制在某个对象前缀下
     * @param bucket       桶名
     * @param resourcePath 对象 key 前缀，形如 "17/"
     * @param actions      允许的动作集合（如 {GetObject, ListBucket} 只读）
     * @return 类型安全的策略对象
     */
    [[nodiscard]] static policy::Policy buildResourcePolicy(
        const std::string& bucket, const std::string& resourcePath,
        const std::vector<policy::Action>& actions);

    /**
     * @brief 构造"单对象策略"：精确到某一个对象 key（无通配符）
     *        用于上传：凭证只能写一个具体文件
     * @param bucket    桶名
     * @param objectKey 对象 key，形如 "17/abc123"
     * @param actions   允许的动作集合（上传场景通常仅 {PutObject}）
     * @return 类型安全的策略对象
     */
    [[nodiscard]] static policy::Policy buildObjectPolicy(
        const std::string& bucket, const std::string& objectKey,
        const std::vector<policy::Action>& actions);

    /**
     * @brief 构造"多前缀策略"：把读权限授予多个对象前缀（用户所有可读位置）
     * @param bucket    桶名
     * @param prefixes  前缀列表，形如 {"17/", "20/", "33/"}
     * @param actions   允许的动作集合（获取场景通常为 {GetObject, ListBucket}）
     * @return 类型安全的策略对象
     */
    [[nodiscard]] static policy::Policy buildPrefixesPolicy(
        const std::string& bucket, const std::vector<std::string>& prefixes,
        const std::vector<policy::Action>& actions);

    // MinIO 配置访问（供上层构造响应/策略时读取）
    [[nodiscard]] std::string getEndpoint() const;
    [[nodiscard]] std::string getBucket() const;
    [[nodiscard]] std::string getRegion() const;
};

} // namespace JettyCat::file::dao