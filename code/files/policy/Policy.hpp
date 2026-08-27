// Policy.hpp —— 对象存储(IAM/S3)访问策略的固定数据类
//
// 职责：以类型安全的方式构造/解析访问策略，避免在业务代码中充斥大量 JSON。
// 提供序列化(toJson/toJsonString)与反序列化(fromJson/parse)函数。
// 固定取值（Effect / Action / Principal）一律使用 enum class，不用手写字符串。
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace JettyCat::file::policy {

// 生效效果
enum class Effect {
    Allow,
    Deny,
};

// 支持的 S3 动作
enum class Action {
    GetObject,     // s3:GetObject
    PutObject,     // s3:PutObject
    DeleteObject,  // s3:DeleteObject
    ListBucket,    // s3:ListBucket
    GetBucketLocation, // s3:GetBucketLocation
    ListBucketMultipartUploads, // s3:ListBucketMultipartUploads
    AbortMultipartUpload,       // s3:AbortMultipartUpload
    ListMultipartUploadParts,   // s3:ListMultipartUploadParts
};

// 条件操作符
enum class ConditionOp {
    StringEquals,
    StringNotEquals,
    StringLike,
    StringNotLike,
    NumericLessThan,
    NumericLessThanEquals,
    NumericGreaterThan,
    NumericGreaterThanEquals,
    IpAddress,
    NotIpAddress,
    Bool,
    DateGreaterThan,
    DateLessThan,
};

// 单个 Statement 条目
class Statement {
public:
    Statement() = default;

    // ---- 链式 setter ----
    Statement& setSid(const std::string& sid)          { m_sid = sid; return *this; }
    Statement& setEffect(Effect effect)                { m_effect = effect; return *this; }
    Statement& addAction(Action action)                { m_actions.push_back(action); return *this; }
    Statement& setActions(const std::vector<Action>& a){ m_actions = a; return *this; }
    Statement& addResource(const std::string& r)       { m_resources.push_back(r); return *this; }
    Statement& setResources(const std::vector<std::string>& r) { m_resources = r; return *this; }
    Statement& addNotResource(const std::string& r)    { m_not_resources.push_back(r); return *this; }
    // 条件: 形如 addCondition(ConditionOp::StringEquals, "s3:prefix", {"user/17/"})
    Statement& addCondition(ConditionOp op, const std::string& key, std::vector<std::string> values);

    // ---- 序列化 / 反序列化 ----
    nlohmann::json toJson() const;
    bool fromJson(const nlohmann::json& j);

    // ---- 只读访问 ----
    [[nodiscard]] bool empty() const { return m_actions.empty() && m_resources.empty(); }

private:
    std::string m_sid;
    std::optional<Effect> m_effect;           // 缺省时 IAM 默认 Deny（不写）
    std::vector<Action> m_actions;
    std::vector<std::string> m_resources;
    std::vector<std::string> m_not_resources;
    std::vector<std::pair<ConditionOp, std::pair<std::string, std::vector<std::string>>>> m_conditions;
};

// 完整策略文档
class Policy {
public:
    Policy() = default;

    // 便捷构造：一个 Allow 语句，指定动作集合与资源集合（最常见的场景）
    static Policy allow(const std::vector<Action>& actions,
                        const std::vector<std::string>& resources);

    // ---- 链式 setter ----
    Policy& setVersion(const std::string& v) { m_version = v; return *this; }
    Policy& setId(const std::string& id)     { m_id = id; return *this; }
    Statement& addStatement()                { m_statements.emplace_back(); return m_statements.back(); }

    // ---- 序列化 / 反序列化 ----
    [[nodiscard]] nlohmann::json toJson() const;
    bool fromJson(const nlohmann::json& j);
    [[nodiscard]] std::string toJsonString() const;
    static bool parse(const std::string& text, Policy& out);

    // 是否没有任何语句（此时不应附加为内联策略）
    [[nodiscard]] bool empty() const { return m_statements.empty(); }

private:
    std::string m_version;               // 缺省 "2012-10-17"
    std::optional<std::string> m_id;
    std::vector<Statement> m_statements;
};

// ---- 枚举 <-> 字符串 辅助（内部 + 外部可复用） ----
std::string effectToString(Effect e);
bool effectFromString(const std::string& s, Effect& out);

std::string actionToString(Action a);
bool actionFromString(const std::string& s, Action& out);

} // namespace JettyCat::file::policy