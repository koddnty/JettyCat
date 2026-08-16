#include "MessageDao.hpp"

namespace chatter::dao {

Task<JettyCat::chat::DBState> MessageDao::fetchFromInbox(
    const JettyCat::chat::userId sender_id, const JettyCat::chat::userId receiver_id,
    const size_t offset, JettyCat::chat::MessageList& message_list) const {
    const std::string sql =
        "select sender_id, msg_type, content, UNIX_TIMESTAMP(send_time) "
        "from user_message "
        "where user_message.sender_id = ? and user_message.receiver_id = ? "
        "order by user_message.send_time desc limit 10 offset ?";

    auto conn = m_db->borrowConn();
    MySQLStmt<int, STMT_Text<36>, STMT_Text<2048>, uint64_t> stmt {conn};

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, sender_id, receiver_id, offset);
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

    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        JettyCat::chat::Message message;
        message.setDate(std::get<3>(it))
            .setContent(std::get<2>(it).toString())
            .setType(std::get<1>(it).toString())
            .setFrom(std::get<0>(it));
        message_list.push_back(message);
    }
    co_return JettyCat::chat::DBState::SUCCESS;
}


Task<JettyCat::chat::DBState> MessageDao::fetchFromGroup(
    const JettyCat::chat::groupId group_id, const size_t offset,
    JettyCat::chat::MessageList& message_list) const {
    const std::string sql =
        "select user_id, msg_type, content, UNIX_TIMESTAMP(send_time) "
        "from group_message "
        "where group_message.group_id = ? "
        "order by group_message.send_time desc limit 10 offset ?";

    auto conn = m_db->borrowConn();
    MySQLStmt<int, STMT_Text<36>, STMT_Text<2048>, uint64_t> stmt {conn};

    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, group_id, offset);
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

    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        JettyCat::chat::Message message;
        message.setDate(std::get<3>(it))
            .setContent(std::get<2>(it).toString())
            .setType(std::get<1>(it).toString())
            .setFrom(std::get<0>(it));
        message_list.push_back(message);
    }
    co_return JettyCat::chat::DBState::SUCCESS;
}

} // namespace chatter::dao
