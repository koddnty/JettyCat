#include "GroupService.hpp"

namespace chatter::service {

[[nodiscard]] Task<resp::HttpResponse> GroupService::addGroup(
                        const JettyCat::chat::userId self_id, const std::string& group_id_str) const {
    resp::HttpResponse http_response;

    // ---------- 参数校验 ----------
    if (group_id_str.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'groupId'");
        co_return http_response;
    }

    JettyCat::chat::groupId group_id = 0;
    bool parse_error = false;
    try {
        group_id = std::stoi(group_id_str);
    } catch (const std::exception&) {
        parse_error = true;
    }
    if (parse_error || group_id <= 0) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Invalid 'groupId'");
        co_return http_response;
    }

    // ---------- 校验目标群聊存在 ----------
    bool exists = false;
    switch (co_await m_group_dao->groupExists(group_id, exists)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }
    if (!exists) {
        http_response.setCode(404).setMsg("NOT_FOUND: groupId does not exist");
        co_return http_response;
    }

    // ---------- 加入群聊 ----------
    switch (co_await m_group_dao->addGroup(self_id, group_id)) {
    case JettyCat::chat::DBState::SUCCESS:
        http_response.setCode(200).setMsg("Group joined");
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        break;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        break;
    }
    co_return http_response;
}


[[nodiscard]] Task<resp::HttpResponse> GroupService::removeGroup(
                        const JettyCat::chat::userId self_id, const std::string& group_id_str) const {
    resp::HttpResponse http_response;

    // ---------- 参数校验 ----------
    if (group_id_str.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'groupId'");
        co_return http_response;
    }

    JettyCat::chat::groupId group_id = 0;
    bool parse_error = false;
    try {
        group_id = std::stoi(group_id_str);
    } catch (const std::exception&) {
        parse_error = true;
    }
    if (parse_error || group_id <= 0) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Invalid 'groupId'");
        co_return http_response;
    }

    // ---------- 退出群聊 ----------
    switch (co_await m_group_dao->removeGroup(self_id, group_id)) {
    case JettyCat::chat::DBState::SUCCESS:
        http_response.setCode(200).setMsg("Group left");
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        break;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        break;
    }
    co_return http_response;
}


[[nodiscard]] Task<resp::HttpResponse> GroupService::getGroupList(
                        const JettyCat::chat::userId self_id) const {
    resp::HttpResponse http_response;

    nlohmann::json groups = nlohmann::json::array();
    switch (co_await m_group_dao->listGroups(self_id, groups)) {
    case JettyCat::chat::DBState::SUCCESS: {
        nlohmann::json data;
        data["groups"] = groups;
        data["count"]  = groups.size();
        http_response.setCode(200).setMsg("ok").setData(data);
        break;
    }
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        break;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        break;
    }
    co_return http_response;
}

} // namespace chatter::service
