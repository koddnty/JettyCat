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

    /**
     * @brief 添加好友（已存在则恢复 status=1）；内部处理 CHECK 约束排序
     * @return SUCCESS=执行成功；TIMEOUT=查询超时；FAILED=数据库失败
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> addFriend(JettyCat::chat::userId self_id,
                                                 JettyCat::chat::userId friend_id) const;

    /**
     * @brief 判断用户是否存在
     * @param exists 输出：true=存在，false=不存在（仅在返回 SUCCESS 时有效）
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> userExists(JettyCat::chat::userId user_id,
                                                  bool& exists) const;

    /**
     * @brief 查询好友列表（双向 union，满足 user_id < friend_id 约束）
     * @param friends_out 输出：好友数组，每项含 friend_id/username/nickname/avatar_url
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> listFriends(JettyCat::chat::userId self_id,
                                                   nlohmann::json& friends_out) const;

    /**
     * @brief 查询某个用户的公开信息（昵称/头像/用户名/ID），用于非好友的陌生用户资料展示
     * @param user_id 要查询的用户 id
     * @param exists   输出：用户是否存在
     * @param profile_out 输出：仅当 exists 为 true 时有意义，含 user_id/username/nickname/avatar_url
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> getPublicProfile(
        JettyCat::chat::userId user_id, bool& exists, nlohmann::json& profile_out) const;

private:
    std::shared_ptr<DbProvider> m_db;       // 数据库依赖
};

}
