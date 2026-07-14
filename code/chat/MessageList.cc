//
// Created by koddnty on 2026/7/14.
//
#include "MessageList.hpp"


namespace JettyCat::chat {
m_sylar::Task<State> sendToUser(const userId& user_id, const MessageList& message_list) {

}

m_sylar::Task<State> sendToGroup(const groupId& group_id, const MessageList& message_list) {
}


m_sylar::Task<State> fetchFromGroup(const groupId& group_id, MessageList& message_list, size_t length) {
}


m_sylar::Task<State> fetchFromInbox(MessageList& message_list, size_t length) {
}
}