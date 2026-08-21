#pragma once

#include "chat/chatter.hpp"
#include "publicHeader.hpp"
#include "dao/DbProvider.hpp"

namespace chatter::dao {

// 好友关系状态（user_friend.status）
enum class FriendStatus : int {
    BLOCKED             = 0,   // 拉黑/删除
    NORMAL              = 1,   // 正常好友
    PENDING_SMALL_LARGE = 2,   // 待确认, 发起方是较小的 id(小到大)
    PENDING_LARGE_SMALL = 3,   // 待确认, 发起方是较大的 id(大到小)
};

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
     * @brief 查询两用户当前好友关系状态
     * @param self_id 当前用户 id
     * @param friend_id 目标用户 id
     * @param status 输出：当前 status（仅当 exists 为 true 时有意义）
     * @param exists 输出：关系行是否存在
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> getFriendStatus(JettyCat::chat::userId self_id,
                                                    JettyCat::chat::userId friend_id,
                                                    int& status, bool& exists) const;

    /**
     * @brief 发送好友申请：写入待确认关系(小到大 status=2, 大到小 status=3)
     *        已存在且被删除/拉黑(status=0)时恢复为待确认; 内部处理 CHECK 约束排序
     * @param self_id 申请发起方(当前用户)
     * @param friend_id 被申请方
     * @return SUCCESS=执行成功；TIMEOUT=查询超时；FAILED=数据库失败
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> sendFriendRequest(JettyCat::chat::userId self_id,
                                                     JettyCat::chat::userId friend_id) const;

    /**
     * @brief 同意好友申请：将被申请方确认的关系置为 status=1(正常好友)
     * @param self_id 当前用户 id(被申请方)
     * @param friend_id 申请方 id
     * @return SUCCESS=执行成功；TIMEOUT=查询超时；FAILED=数据库失败
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> agreeFriend(JettyCat::chat::userId self_id,
                                               JettyCat::chat::userId friend_id) const;

    /**
     * @brief 查询发给当前用户的所有待确认好友申请（对方发起的申请）
     * @param requests_out 输出：申请列表, 每项含 friend_id/username/nickname/avatar_url(申请方信息)
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> listFriendRequests(JettyCat::chat::userId self_id,
                                                     nlohmann::json& requests_out) const;

    /**
     * @brief 判断用户是否存在
     * @param exists 输出：true=存在，false=不存在（仅在返回 SUCCESS 时有效）
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> userExists(JettyCat::chat::userId user_id,
                                                  bool& exists) const;

    /**
     * @brief 按对外账号(users.user_id)查询对内路由 id(users.user_snow_id)
     * @param exists 输出：true=存在，false=不存在（仅在返回 SUCCESS 时有效）
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> getUserSnowIdByUserId(const std::string& user_id,
                                                            JettyCat::chat::userId& snow_id,
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
