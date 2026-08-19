#include "GroupDao.hpp"

namespace chatter::dao {

Task<JettyCat::chat::DBState> GroupDao::groupExists(const JettyCat::chat::groupId group_id,
                                               bool& exists) const {
    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t> stmt {conn};
    const std::string sql = "select group_snow_id from `group` where group_snow_id = ?";

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, group_id);
    }
    if (state != IOState::SUCCESS) {
        co_return state == IOState::TIMEOUT ? JettyCat::chat::DBState::TIMEOUT
                                            : JettyCat::chat::DBState::FAILED;
    }
    if (co_await stmt.co_storeAll() != IOState::SUCCESS) {
        co_return JettyCat::chat::DBState::FAILED;
    }
    if (co_await stmt.co_fetchAll() != IOState::SUCCESS) {
        co_return JettyCat::chat::DBState::FAILED;
    }

    exists = !stmt.getResult().getAll().empty();
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> GroupDao::getGroupByGroupId(
    const JettyCat::chat::groupId group_id, JettyCat::chat::groupId& group_snow_id,
    std::string& group_name, bool& exists) const {
    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t, STMT_Text<256>> stmt {conn};
    // 对外的 group_id 是用户可感知的群id, 对内路由使用 group_snow_id
    const std::string sql = "select group_snow_id, group_name from `group` where group_id = ?";

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, group_id);
    }
    if (state != IOState::SUCCESS) {
        co_return state == IOState::TIMEOUT ? JettyCat::chat::DBState::TIMEOUT
                                            : JettyCat::chat::DBState::FAILED;
    }
    if (co_await stmt.co_storeAll() != IOState::SUCCESS) {
        co_return JettyCat::chat::DBState::FAILED;
    }
    if (co_await stmt.co_fetchAll() != IOState::SUCCESS) {
        co_return JettyCat::chat::DBState::FAILED;
    }

    auto rows = stmt.getResult().getAll();
    if (rows.empty()) {
        exists = false;
        co_return JettyCat::chat::DBState::SUCCESS;
    }
    exists = true;
    group_snow_id = std::get<0>(rows.front());
    group_name = std::get<1>(rows.front()).toString();
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> GroupDao::addGroup(const JettyCat::chat::userId user_id,
                                             const JettyCat::chat::groupId group_id) const {
    auto conn = m_db->borrowConn();
    MySQLStmt stmt {conn};
    const std::string sql =
        "insert into user_group (user_snow_id, group_snow_id, identity) "
        "values (?, ?, 'member') "
        "on duplicate key update join_time = current_timestamp";

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, user_id, group_id);
    }

    switch (state) {
    case IOState::SUCCESS:
        co_return JettyCat::chat::DBState::SUCCESS;
    case IOState::TIMEOUT:
        co_return JettyCat::chat::DBState::TIMEOUT;
    default:
        co_return JettyCat::chat::DBState::FAILED;
    }
}


Task<JettyCat::chat::DBState> GroupDao::removeGroup(const JettyCat::chat::userId user_id,
                                                const JettyCat::chat::groupId group_id) const {
    auto conn = m_db->borrowConn();
    MySQLStmt stmt {conn};
    const std::string sql = "delete from user_group where user_snow_id = ? and group_snow_id = ?";

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, user_id, group_id);
    }

    switch (state) {
    case IOState::SUCCESS:
        co_return JettyCat::chat::DBState::SUCCESS;
    case IOState::TIMEOUT:
        co_return JettyCat::chat::DBState::TIMEOUT;
    default:
        co_return JettyCat::chat::DBState::FAILED;
    }
}


Task<JettyCat::chat::DBState> GroupDao::listGroups(const JettyCat::chat::userId user_id,
                                               nlohmann::json& groups_out) const {
    const std::string sql =
        "select g.group_snow_id, g.group_id, g.group_name, ug.identity, UNIX_TIMESTAMP(ug.join_time) "
        "from user_group ug "
        "join `group` g on ug.group_snow_id = g.group_snow_id "
        "where ug.user_snow_id = ?";

    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t, int64_t, STMT_Text<256>, STMT_Text<20>, uint64_t> stmt {conn};

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, user_id);
    }
    if (state != IOState::SUCCESS) {
        co_return state == IOState::TIMEOUT ? JettyCat::chat::DBState::TIMEOUT
                                            : JettyCat::chat::DBState::FAILED;
    }
    if (co_await stmt.co_storeAll() != IOState::SUCCESS) {
        co_return JettyCat::chat::DBState::FAILED;
    }
    if (co_await stmt.co_fetchAll() != IOState::SUCCESS) {
        co_return JettyCat::chat::DBState::FAILED;
    }

    nlohmann::json groups = nlohmann::json::array();
    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        nlohmann::json group_json;
        group_json["group_snow_id"] = std::get<0>(it);   // 对内路由用群组 snow id
        group_json["group_id"]      = std::get<1>(it);   // 对外暴露的群id
        group_json["group_name"]    = std::get<2>(it).toString();
        group_json["identity"]      = std::get<3>(it).toString();
        group_json["join_time"]     = std::get<4>(it);
        groups.push_back(group_json);
    }
    groups_out = std::move(groups);
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> GroupDao::listJoinedGroupIds(
    const JettyCat::chat::userId user_id,
    std::vector<JettyCat::chat::groupId>& group_ids_out) const {
    const std::string sql = "select group_snow_id from user_group where user_snow_id = ?";

    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t> stmt {conn};

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, user_id);
    }
    if (state != IOState::SUCCESS) {
        co_return state == IOState::TIMEOUT ? JettyCat::chat::DBState::TIMEOUT
                                            : JettyCat::chat::DBState::FAILED;
    }
    if (co_await stmt.co_storeAll() != IOState::SUCCESS) {
        co_return JettyCat::chat::DBState::FAILED;
    }
    if (co_await stmt.co_fetchAll() != IOState::SUCCESS) {
        co_return JettyCat::chat::DBState::FAILED;
    }

    std::vector<JettyCat::chat::groupId> group_ids;
    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        group_ids.push_back(std::get<0>(it));
    }
    group_ids_out = std::move(group_ids);
    co_return JettyCat::chat::DBState::SUCCESS;
}

} // namespace chatter::dao
