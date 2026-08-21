#include "FriendDao.hpp"


namespace chatter::dao {


Task<JettyCat::chat::DBState> FriendDao::removeFriend(const JettyCat::chat::userId self_id,
                                                  const JettyCat::chat::userId friend_id) const {
    // user_friend 表历史上有 CHECK 约束 user_snow_id < friend_snow_id，写入前排序以保证兼容。
    auto min_id = std::min(self_id, friend_id);
    auto max_id = std::max(self_id, friend_id);

    auto conn = m_db->borrowConn();
    MySQLStmt stmt {conn};
    const std::string sql =
        "update user_friend set status = 0 "
        "where user_snow_id = ? and friend_snow_id = ?";

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


Task<JettyCat::chat::DBState> FriendDao::getFriendStatus(const JettyCat::chat::userId self_id,
                                                  const JettyCat::chat::userId friend_id,
                                                  int& status, bool& exists) const {
    // 兼容历史 CHECK 约束 user_snow_id < friend_snow_id，排序后查询
    auto min_id = std::min(self_id, friend_id);
    auto max_id = std::max(self_id, friend_id);

    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t> stmt {conn};
    const std::string sql =
        "select status from user_friend where user_snow_id = ? and friend_snow_id = ?";

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, min_id, max_id);
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
        status = -1;
        co_return JettyCat::chat::DBState::SUCCESS;
    }
    exists = true;
    status = std::get<0>(rows.front());
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> FriendDao::sendFriendRequest(const JettyCat::chat::userId self_id,
                                                   const JettyCat::chat::userId friend_id) const {
    // 兼容历史 CHECK 约束 user_snow_id < friend_snow_id，排序后写入
    auto min_id = std::min(self_id, friend_id);
    auto max_id = std::max(self_id, friend_id);
    // 小到大(发起方为较小id)=2, 大到小(发起方为较大id)=3
    int pending_status = (self_id < friend_id)
        ? static_cast<int>(FriendStatus::PENDING_SMALL_LARGE)
        : static_cast<int>(FriendStatus::PENDING_LARGE_SMALL);

    auto conn = m_db->borrowConn();
    MySQLStmt stmt {conn};
    const std::string sql =
        "insert into user_friend (user_snow_id, friend_snow_id, status) "
        "values (?, ?, ?) "
        "on duplicate key update status = if(status = 0, VALUES(status), status)";

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, min_id, max_id, pending_status);
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


Task<JettyCat::chat::DBState> FriendDao::agreeFriend(const JettyCat::chat::userId self_id,
                                             const JettyCat::chat::userId friend_id) const {
    // 兼容历史 CHECK 约束 user_snow_id < friend_snow_id，排序后更新
    auto min_id = std::min(self_id, friend_id);
    auto max_id = std::max(self_id, friend_id);

    auto conn = m_db->borrowConn();
    MySQLStmt stmt {conn};
    const std::string sql =
        "update user_friend set status = 1 "
        "where user_snow_id = ? and friend_snow_id = ? and status in (2, 3)";

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


Task<JettyCat::chat::DBState> FriendDao::listFriendRequests(const JettyCat::chat::userId self_id,
                                                    nlohmann::json& requests_out) const {
    // 对方发起的待确认申请:
    //   status=2(小到大) 时发起方是 user_snow_id, 被申请方是 friend_snow_id
    //   status=3(大到小) 时发起方是 friend_snow_id, 被申请方是 user_snow_id
    // 因此筛选被申请方 = self_id 的行, 并 join 出发起方的公开信息。
    const std::string sql =
        "select u.user_snow_id, u.user_id, u.user_name, u.avatar_url "
        "from user_friend f "
        "join users u on u.user_snow_id = "
        "  (case when f.status = 2 then f.user_snow_id else f.friend_snow_id end) "
        "where (f.status = 2 and f.friend_snow_id = ?) "
        "   or (f.status = 3 and f.user_snow_id = ?)";

    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t, STMT_Text<50>, STMT_Text<100>, STMT_Text<500>> stmt {conn};

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

    nlohmann::json requests = nlohmann::json::array();
    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        nlohmann::json req_json;
        req_json["friend_id"]      = std::to_string(std::get<0>(it)); // 对内 user_snow_id, 字符串传输
        req_json["username"]       = std::get<1>(it).toString();     // 对外 user_id(账号)
        req_json["nickname"]       = std::get<2>(it).toString();     // user_name(昵称)
        req_json["avatar_url"]     = std::get<3>(it).toString();
        requests.push_back(req_json);
    }
    requests_out = std::move(requests);
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> FriendDao::userExists(const JettyCat::chat::userId user_id,
                                                bool& exists) const {
    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t> stmt {conn};
    const std::string sql = "select user_snow_id from users where user_snow_id = ?";

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


Task<JettyCat::chat::DBState> FriendDao::getUserSnowIdByUserId(const std::string& user_id,
                                                         JettyCat::chat::userId& snow_id,
                                                         bool& exists) const {
    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t> stmt {conn};
    // 对外的 user_id 即账号列, 对内路由使用 user_snow_id
    const std::string sql = "select user_snow_id from users where user_id = ?";

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

    auto rows = stmt.getResult().getAll();
    if (rows.empty()) {
        exists = false;
        co_return JettyCat::chat::DBState::SUCCESS;
    }
    exists = true;
    snow_id = std::get<0>(rows.front());
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> FriendDao::listFriends(const JettyCat::chat::userId self_id,
                                                 nlohmann::json& friends_out) const {
    // user_friend 表历史约束 user_snow_id < friend_snow_id，因此双向查询
    const std::string sql =
        "select u.user_snow_id, u.user_id, u.user_name, u.avatar_url "
        "from user_friend f "
        "join users u on u.user_snow_id = f.friend_snow_id "
        "where f.user_snow_id = ? and f.status = 1 "
        "union "
        "select u.user_snow_id, u.user_id, u.user_name, u.avatar_url "
        "from user_friend f "
        "join users u on u.user_snow_id = f.user_snow_id "
        "where f.friend_snow_id = ? and f.status = 1";

    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t, STMT_Text<50>, STMT_Text<100>, STMT_Text<500>> stmt {conn};

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
        friend_json["friend_id"]  = std::to_string(std::get<0>(it)); // 对内 user_snow_id, 以字符串传输避免精度丢失
        friend_json["username"]   = std::get<1>(it).toString();     // 对外 user_id(账号)
        friend_json["nickname"]   = std::get<2>(it).toString();     // user_name(昵称)
        friend_json["avatar_url"] = std::get<3>(it).toString();
        friends.push_back(friend_json);
    }
    friends_out = std::move(friends);
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> FriendDao::getPublicProfile(const JettyCat::chat::userId user_id,
                                                       bool& exists, nlohmann::json& profile_out) const {
    const std::string sql =
        "select u.user_snow_id, u.user_id, u.user_name, u.avatar from users u where u.user_snow_id = ?";

    auto conn = m_db->borrowConn();
    MySQLStmt<int64_t, STMT_Text<50>, STMT_Text<100>, STMT_Text<500>> stmt {conn};

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

    auto rows = stmt.getResult().getAll();
    if (rows.empty()) {
        exists = false;
        co_return JettyCat::chat::DBState::SUCCESS;
    }

    exists = true;
    nlohmann::json profile;
    {
        auto& row = rows.front();
        profile["user_id"]    = std::to_string(std::get<0>(row)); // 对内 user_snow_id, 以字符串传输避免精度丢失
        profile["username"]   = std::get<1>(row).toString();      // 对外 user_id(账号)
        profile["nickname"]   = std::get<2>(row).toString();      // user_name(昵称)
        profile["avatar_url"] = std::get<3>(row).toString();
    }
    profile_out = std::move(profile);
    co_return JettyCat::chat::DBState::SUCCESS;
}

} // namespace chatter::dao