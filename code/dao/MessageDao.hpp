// MessageDao.hpp —— 消息的数据访问层
//
// 职责：只负责消息相关 SQL 读写（收件箱/群消息），不关心 HTTP/JSON 响应。
// 连接来源由构造函数注入的 DbProvider 提供（依赖注入）。
#pragma once

#include "chat/Message.hpp"
#include "publicHeader.hpp"
#include "dao/DbProvider.hpp"

namespace chatter::dao {

class MessageDao {
public:
    explicit MessageDao(std::shared_ptr<DbProvider> db) : m_db(std::move(db)) {}

    /**
     * @brief 拉取私聊消息（sender -> receiver），每页 10 条，按时间倒序
     * @param message_list 输出：消息列表
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> fetchFromInbox(
        JettyCat::chat::userId sender_id, JettyCat::chat::userId receiver_id,
        size_t offset, JettyCat::chat::MessageList& message_list) const;

    /**
     * @brief 拉取群聊消息，每页 10 条，按时间倒序
     * @param message_list 输出：消息列表
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> fetchFromGroup(
        JettyCat::chat::groupId group_id, size_t offset,
        JettyCat::chat::MessageList& message_list) const;

private:
    std::shared_ptr<DbProvider> m_db;
};

} // namespace chatter::dao
