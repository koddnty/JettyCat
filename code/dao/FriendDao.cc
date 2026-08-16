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



}