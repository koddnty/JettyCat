#include "FriendDao.hpp"


namespace chatter::dao {


Task<JettyCat::chat::DBState> FriendDao::removeFriend(const JettyCat::chat::userId self_id,
                                                  const JettyCat::chat::userId friend_id) const {
    // user_friend 表有 CHECK 约束 user_id < friend_id，所以写入前排序。
    // 这条 SQL 层规则属于 DAO 的职责，从原来散落在 handler 里挪到这里。
    auto min_id = std::min(self_id, friend_id);
    auto max_id = std::max(self_id, friend_id);

    auto conn = m_db->borrowConn();
    MySQLStmt stmt {conn};
    const std::string sql =
        "update user_friend set status = 0 "
        "where user_id = ? and friend_id = ?";

    // retry 循环（沿用原 3 次重试语义）
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, min_id, max_id);
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


Task<JettyCat::chat::DBState> FriendDao::addFriend(const JettyCat::chat::userId self_id,
                                               const JettyCat::chat::userId friend_id) const {
    // CHECK 约束 user_id < friend_id，排序后写入（与 removeFriend 一致）
    auto min_id = std::min(self_id, friend_id);
    auto max_id = std::max(self_id, friend_id);

    auto conn = m_db->borrowConn();
    MySQLStmt stmt {conn};
    const std::string sql =
        "insert into user_friend (user_id, friend_id, status) "
        "values (?, ?, 1) "
        "on duplicate key update status = 1";

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, min_id, max_id);
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


Task<JettyCat::chat::DBState> FriendDao::userExists(const JettyCat::chat::userId user_id,
                                                bool& exists) const {
    auto conn = m_db->borrowConn();
    MySQLStmt<int> stmt {conn};
    const std::string sql = "select user_id from users where user_id = ?";

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

    exists = !stmt.getResult().getAll().empty();
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> FriendDao::listFriends(const JettyCat::chat::userId self_id,
                                                 nlohmann::json& friends_out) const {
    // user_friend 表约束 user_id < friend_id，因此双向查询
    const std::string sql =
        "select u.user_id, u.username, u.nickname, u.avatar_url "
        "from user_friend f "
        "join users u on u.user_id = f.friend_id "
        "where f.user_id = ? and f.status = 1 "
        "union "
        "select u.user_id, u.username, u.nickname, u.avatar_url "
        "from user_friend f "
        "join users u on u.user_id = f.user_id "
        "where f.friend_id = ? and f.status = 1";

    auto conn = m_db->borrowConn();
    MySQLStmt<int, STMT_Text<50>, STMT_Text<100>, STMT_Text<500>> stmt {conn};

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, self_id, self_id);
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

    nlohmann::json friends = nlohmann::json::array();
    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        nlohmann::json friend_json;
        friend_json["friend_id"]  = std::get<0>(it);
        friend_json["username"]   = std::get<1>(it).toString();
        friend_json["nickname"]   = std::get<2>(it).toString();
        friend_json["avatar_url"] = std::get<3>(it).toString();
        friends.push_back(friend_json);
    }
    friends_out = std::move(friends);
    co_return JettyCat::chat::DBState::SUCCESS;
}

} // namespace chatter::dao