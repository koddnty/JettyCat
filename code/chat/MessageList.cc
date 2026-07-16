//
// Created by koddnty on 2026/7/14.
//
#include "MessageList.hpp"



namespace JettyCat::chat {
static m_sylar::Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");


std::string Message::dump() const {
    nlohmann::json j{};
    j["date"] = m_date;
    j["from"] = m_from;
    j["type"] = m_type;
    j["content"] = m_content;
    return j.dump();
}

m_sylar::Task<State> sendToUser(const userId& user_id, const MessageList& message_list) {
    std::stringstream cmd;
    cmd << "insert into user_message (sender_id, receiver_id, content, extra) values (";
    for (auto msg_it = message_list.begin(); msg_it != message_list.end(); ++msg_it) {
        cmd << std::to_string(msg_it->getFrom()) << ", "
            << std::to_string(user_id) << ", '"
            << msg_it->getContent() << "', '"
            << msg_it->dump()<< "'"
            << (std::next(msg_it) == message_list.end() ? "\n);" : ",\n");
    }

    // 发送并处理
    M_SYLAR_LOG_DEBUG(g_logger) << cmd.str();
    const MySQLResp::ptr state =  co_await m_sylar::DB::Mysql::getInstance()->executeQuery(cmd.str());
    if (state) {
        if (state->getState() == IOState::State::SUCCESS) {
            co_return State::SUCCESS;
        }
        else if (state->getState() == IOState::State::TIMEOUT){
            co_return State::TIMEOUT;
        }
        else {
            M_SYLAR_LOG_ERROR(g_logger) << "sql execute state: " << state->getState() << ", failed to execute sql: " << cmd.str();
            co_return State::FAILED;
        }
    }
    M_SYLAR_ASSERT2(false, "should never reach here");
    co_return State::FAILED;
}


m_sylar::Task<State> sendToGroup(const groupId& group_id, const MessageList& message_list) {

}


m_sylar::Task<State> fetchFromGroup(const groupId& group_id, MessageList& message_list) {

}


m_sylar::Task<State> fetchFromInbox(const userId& user_id, MessageList& message_list) {

}
};