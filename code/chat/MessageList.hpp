//
// Created by koddnty on 2026/7/14.
//

#ifndef JETTYCAT_MESSAGE_HPP
#define JETTYCAT_MESSAGE_HPP
#include <coroutine/coro20/task.hpp>

#endif //JETTYCAT_MESSAGE_HPP

/**
 * 聊天消息处理,负责消息持久化到数据库
 *
 */

namespace JettyCat::chat {
using groupId = std::string;        // 群聊id
using userId = std::string;         // 用户id


// 消息
class Message {
public:
    enum class Type {
        TXT,
        PICTURE
    };

    Message() = default;
    Message(const Message&) = default;
    Message& operator=(const Message& other) = default;


    inline Message& setContent(const std::string& content) {m_content = content; return *this;}
    inline Message& setType(const Type type) {m_type = type; return *this;}
    inline Message& setFrom(const userId& from) {m_from = from; return *this;}
    inline Message& setDate(const uint64_t date) {m_date = date; return *this;}

    inline const std::string& getContent() const {return m_content;}
    inline const userId& getFrom() const  {return m_from;}
    inline uint64_t getDate() const  {return m_date;}
    inline Type getType() const {return m_type;}

private:
    uint64_t m_date{0};             // 时间戳
    userId m_from;                  // 来源
    Type m_type {Type::TXT};        // 类型
    std::string m_content;          // 根据Type决定消息类型
};



// 消息列表
using MessageList = std::list<Message>;



enum class State {        // 数据库访问状态定义
    SUCCESS,
    TIMEOUT,
    FAILED
};

static m_sylar::Task<State>sendToUser(const userId& user_id, const MessageList& message_list);                  // 发送到用户收件箱
static m_sylar::Task<State>sendToGroup(const groupId& group_id, const MessageList& message_list);               // 发送到群聊发件箱,等待其他用户拉取

static m_sylar::Task<State>fetchFromGroup(const groupId& group_id, MessageList& message_list, size_t length);       // 从群聊接受消息
static m_sylar::Task<State>fetchFromInbox(MessageList& message_list, size_t length);                              // 从收件箱接受消息


};

