#include "GroupDao.hpp"

namespace chatter::dao {

Task<JettyCat::chat::DBState> GroupDao::groupExists(const JettyCat::chat::groupId group_id,
                                               bool& exists) const {
    auto conn = m_db->borrowConn();
    MySQLStmt<int> stmt {conn};
    const std::string sql = "select group_id from `group` where group_id = ?";

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


Task<JettyCat::chat::DBState> GroupDao::addGroup(const JettyCat::chat::userId user_id,
                                             const JettyCat::chat::groupId group_id) const {
    auto conn = m_db->borrowConn();
    MySQLStmt stmt {conn};
    const std::string sql =
        "insert into user_group (user_id, group_id, identity) "
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
    const std::string sql = "delete from user_group where user_id = ? and group_id = ?";

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
        "select g.group_id, g.group_name, ug.identity, UNIX_TIMESTAMP(ug.join_time) "
        "from user_group ug "
        "join `group` g on ug.group_id = g.group_id "
        "where ug.user_id = ?";

    auto conn = m_db->borrowConn();
    MySQLStmt<int, STMT_Text<256>, STMT_Text<20>, uint64_t> stmt {conn};

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
        group_json["group_id"]   = std::get<0>(it);
        group_json["group_name"] = std::get<1>(it).toString();
        group_json["identity"]   = std::get<2>(it).toString();
        group_json["join_time"]  = std::get<3>(it);
        groups.push_back(group_json);
    }
    groups_out = std::move(groups);
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> GroupDao::listJoinedGroupIds(
    const JettyCat::chat::userId user_id,
    std::vector<JettyCat::chat::groupId>& group_ids_out) const {
    const std::string sql = "select group_id from user_group where user_id = ?";

    auto conn = m_db->borrowConn();
    MySQLStmt<int> stmt {conn};

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
