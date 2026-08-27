#include "Policy.hpp"

namespace JettyCat::file::policy {


// 枚举 <-> 字符串
std::string effectToString(Effect e) {
    switch (e) {
    case Effect::Allow: return "Allow";
    case Effect::Deny:  return "Deny";
    }
    return "Allow";
}


bool effectFromString(const std::string& s, Effect& out) {
    if (s == "Allow") { out = Effect::Allow; return true; }
    if (s == "Deny")  { out = Effect::Deny;  return true; }
    return false;
}


std::string actionToString(Action a) {
    switch (a) {
    case Action::GetObject:                   return "s3:GetObject";
    case Action::PutObject:                   return "s3:PutObject";
    case Action::DeleteObject:                return "s3:DeleteObject";
    case Action::ListBucket:                  return "s3:ListBucket";
    case Action::GetBucketLocation:           return "s3:GetBucketLocation";
    case Action::ListBucketMultipartUploads:  return "s3:ListBucketMultipartUploads";
    case Action::AbortMultipartUpload:        return "s3:AbortMultipartUpload";
    case Action::ListMultipartUploadParts:    return "s3:ListMultipartUploadParts";
    }
    return "";
}


bool actionFromString(const std::string& s, Action& out) {
    if (s == "s3:GetObject")                  { out = Action::GetObject; return true; }
    if (s == "s3:PutObject")                  { out = Action::PutObject; return true; }
    if (s == "s3:DeleteObject")               { out = Action::DeleteObject; return true; }
    if (s == "s3:ListBucket")                 { out = Action::ListBucket; return true; }
    if (s == "s3:GetBucketLocation")          { out = Action::GetBucketLocation; return true; }
    if (s == "s3:ListBucketMultipartUploads") { out = Action::ListBucketMultipartUploads; return true; }
    if (s == "s3:AbortMultipartUpload")       { out = Action::AbortMultipartUpload; return true; }
    if (s == "s3:ListMultipartUploadParts")   { out = Action::ListMultipartUploadParts; return true; }
    return false;
}


// Condition 序列化（最小实现：字符串类型条件按值数组输出）
namespace {
std::string conditionOpToString(ConditionOp op) {
    switch (op) {
    case ConditionOp::StringEquals:           return "StringEquals";
    case ConditionOp::StringNotEquals:        return "StringNotEquals";
    case ConditionOp::StringLike:             return "StringLike";
    case ConditionOp::StringNotLike:          return "StringNotLike";
    case ConditionOp::NumericLessThan:        return "NumericLessThan";
    case ConditionOp::NumericLessThanEquals:  return "NumericLessThanEquals";
    case ConditionOp::NumericGreaterThan:     return "NumericGreaterThan";
    case ConditionOp::NumericGreaterThanEquals: return "NumericGreaterThanEquals";
    case ConditionOp::IpAddress:              return "IpAddress";
    case ConditionOp::NotIpAddress:           return "NotIpAddress";
    case ConditionOp::Bool:                   return "Bool";
    case ConditionOp::DateGreaterThan:        return "DateGreaterThan";
    case ConditionOp::DateLessThan:           return "DateLessThan";
    }
    return "";
}
} // namespace



// Statement
Statement& Statement::addCondition(ConditionOp op, const std::string& key,
                                   std::vector<std::string> values) {
    m_conditions.emplace_back(op, std::make_pair(key, std::move(values)));
    return *this;
}


nlohmann::json Statement::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    if (!m_sid.empty())                    j["Sid"] = m_sid;
    if (m_effect.has_value())              j["Effect"] = effectToString(*m_effect);

    if (!m_actions.empty()) {
        nlohmann::json acts = nlohmann::json::array();
        for (auto a : m_actions) acts.push_back(actionToString(a));
        j["Action"] = acts;
    }
    if (!m_resources.empty()) {
        nlohmann::json res = nlohmann::json::array();
        for (const auto& r : m_resources) res.push_back(r);
        j["Resource"] = res;
    }
    if (!m_not_resources.empty()) {
        nlohmann::json res = nlohmann::json::array();
        for (const auto& r : m_not_resources) res.push_back(r);
        j["NotResource"] = res;
    }
    if (!m_conditions.empty()) {
        nlohmann::json cond = nlohmann::json::object();
        for (const auto& [op, kv] : m_conditions) {
            nlohmann::json values = nlohmann::json::array();
            for (const auto& v : kv.second) values.push_back(v);
            cond[conditionOpToString(op)][kv.first] = values;
        }
        j["Condition"] = cond;
    }
    return j;
}


bool Statement::fromJson(const nlohmann::json& j) {
    m_actions.clear(); m_resources.clear(); m_not_resources.clear(); m_conditions.clear();
    m_effect.reset(); m_sid.clear();

    if (j.contains("Sid") && j["Sid"].is_string())      m_sid = j["Sid"].get<std::string>();
    if (j.contains("Effect") && j["Effect"].is_string()) {
        if (!effectFromString(j["Effect"].get<std::string>(), m_effect.emplace())) return false;
    }
    if (j.contains("Action")) {
        const auto& a = j["Action"];
        if (a.is_string()) {
            Action act;
            if (!actionFromString(a.get<std::string>(), act)) return false;
            m_actions.push_back(act);
        } else if (a.is_array()) {
            for (const auto& s : a) {
                Action act;
                if (!s.is_string() || !actionFromString(s.get<std::string>(), act)) return false;
                m_actions.push_back(act);
            }
        }
    }
    if (j.contains("Resource")) {
        const auto& r = j["Resource"];
        if (r.is_string()) m_resources.push_back(r.get<std::string>());
        else if (r.is_array())
            for (const auto& s : r)
                if (s.is_string()) m_resources.push_back(s.get<std::string>());
    }
    if (j.contains("NotResource")) {
        const auto& r = j["NotResource"];
        if (r.is_string()) m_not_resources.push_back(r.get<std::string>());
        else if (r.is_array())
            for (const auto& s : r)
                if (s.is_string()) m_not_resources.push_back(s.get<std::string>());
    }
    // Condition：读取出已知条件操作符下的所有 key/value，原样保留值（含单值）
    if (j.contains("Condition") && j["Condition"].is_object()) {
        for (auto& [op, kv] : j["Condition"].items()) {
            ConditionOp cop;
            // 条件操作符字符串 -> 枚举；未知操作符跳过（保持向前兼容）
            static const std::vector<std::pair<std::string, ConditionOp>> ops = {
                {"StringEquals", ConditionOp::StringEquals},
                {"StringNotEquals", ConditionOp::StringNotEquals},
                {"StringLike", ConditionOp::StringLike},
                {"StringNotLike", ConditionOp::StringNotLike},
                {"NumericLessThan", ConditionOp::NumericLessThan},
                {"NumericLessThanEquals", ConditionOp::NumericLessThanEquals},
                {"NumericGreaterThan", ConditionOp::NumericGreaterThan},
                {"NumericGreaterThanEquals", ConditionOp::NumericGreaterThanEquals},
                {"IpAddress", ConditionOp::IpAddress},
                {"NotIpAddress", ConditionOp::NotIpAddress},
                {"Bool", ConditionOp::Bool},
                {"DateGreaterThan", ConditionOp::DateGreaterThan},
                {"DateLessThan", ConditionOp::DateLessThan},
            };
            auto it = std::find_if(ops.begin(), ops.end(),
                                   [&](const auto& p) { return p.first == op; });
            if (it == ops.end()) continue;
            cop = it->second;
            if (!kv.is_object()) continue;
            for (auto& [key, val] : kv.items()) {
                std::vector<std::string> vals;
                if (val.is_string()) vals.push_back(val.get<std::string>());
                else if (val.is_array())
                    for (const auto& s : val)
                        if (s.is_string()) vals.push_back(s.get<std::string>());
                if (!vals.empty()) addCondition(cop, key, vals);
            }
        }
    }
    return true;
}






// Policy
Policy Policy::allow(const std::vector<Action>& actions,
                     const std::vector<std::string>& resources) {
    Policy p;
    p.m_version = "2012-10-17";
    Statement& s = p.addStatement();
    s.setEffect(Effect::Allow);
    s.setActions(actions);
    s.setResources(resources);
    return p;
}


nlohmann::json Policy::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    j["Version"] = m_version.empty() ? "2012-10-17" : m_version;
    if (m_id.has_value()) j["Id"] = *m_id;
    nlohmann::json stmts = nlohmann::json::array();
    for (const auto& s : m_statements) {
        if (!s.empty()) stmts.push_back(s.toJson());
    }
    j["Statement"] = stmts;
    return j;
}


bool Policy::fromJson(const nlohmann::json& j) {
    m_statements.clear(); m_id.reset(); m_version.clear();
    if (!j.is_object()) return false;

    if (j.contains("Version") && j["Version"].is_string())
        m_version = j["Version"].get<std::string>();
    if (j.contains("Id") && j["Id"].is_string())
        m_id = j["Id"].get<std::string>();

    if (j.contains("Statement")) {
        const auto& st = j["Statement"];
        if (st.is_object()) {
            Statement s;
            if (!s.fromJson(st)) return false;
            m_statements.push_back(s);
        } else if (st.is_array()) {
            for (const auto& s : st) {
                Statement stmt;
                if (!stmt.fromJson(s)) return false;
                m_statements.push_back(stmt);
            }
        }
    }
    return true;
}

std::string Policy::toJsonString() const {
    return toJson().dump();
}

bool Policy::parse(const std::string& text, Policy& out) {
    try {
        auto j = nlohmann::json::parse(text);
        return out.fromJson(j);
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace JettyCat::file::policy