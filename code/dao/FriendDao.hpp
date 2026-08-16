#pragma once

#include "chat/chatter.hpp"
#include "publicHeader.hpp"
#include "dao/DbProvider.hpp"

namespace chatter::dao {

class FriendDao {
public:
    explicit FriendDao(std::shared_ptr<DbProvider> db) : m_db(std::move(db)) {}

    /**
     * @brief 标记删除好友关系（status = 0，表示拉黑/删除）
     * @return SUCCESS=执行成功；TIMEOUT=查询超时；FAILED=数据库失败
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> removeFriend(JettyCat::chat::userId self_id,
                                                    JettyCat::chat::userId friend_id) const;

private:
    std::shared_ptr<DbProvider> m_db;       // 数据库依赖
};

}
