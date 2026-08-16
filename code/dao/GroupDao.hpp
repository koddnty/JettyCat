// GroupDao.hpp —— 群组关系的数据访问层
//
// 职责：只负责"群组关系"相关的 SQL 读写，不关心 HTTP 协议、不关心 JSON 响应。
// 连接来源由构造函数注入的 DbProvider 提供（依赖注入）。
#pragma once

#include "chat/chatter.hpp"
#include "publicHeader.hpp"
#include "dao/DbProvider.hpp"

namespace chatter::dao {

class GroupDao {
public:
    explicit GroupDao(std::shared_ptr<DbProvider> db) : m_db(std::move(db)) {}

    /**
     * @brief 判断群组是否存在
     * @param exists 输出：true=存在，false=不存在（仅在返回 SUCCESS 时有效）
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> groupExists(JettyCat::chat::groupId group_id,
                                                   bool& exists) const;

    /**
     * @brief 加入群聊（已存在则刷新加入时间）
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> addGroup(JettyCat::chat::userId user_id,
                                                JettyCat::chat::groupId group_id) const;

    /**
     * @brief 退出群聊（user_group 表无 status，物理删除）
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> removeGroup(JettyCat::chat::userId user_id,
                                                   JettyCat::chat::groupId group_id) const;

    /**
     * @brief 查询加入的群聊列表
     * @param groups_out 输出：群聊数组，每项含 group_id/group_name/identity/join_time
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> listGroups(JettyCat::chat::userId user_id,
                                                  nlohmann::json& groups_out) const;

    /**
     * @brief 查询用户加入的所有群 group_id 列表（轻量，仅 group_id）
     *        用于 WS 上线时批量把用户登记进群聊在线集合
     * @param group_ids_out 输出：群 id 列表
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> listJoinedGroupIds(
        JettyCat::chat::userId user_id,
        std::vector<JettyCat::chat::groupId>& group_ids_out) const;

private:
    std::shared_ptr<DbProvider> m_db;
};

} // namespace chatter::dao
