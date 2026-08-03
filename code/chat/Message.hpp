//
// Created by koddnty on 2026/7/14.
//
#pragma once

#ifndef JETTYCAT_MESSAGE_HPP
#define JETTYCAT_MESSAGE_HPP
#include <coroutine/coro20/task.hpp>
#include "publicHeader.hpp"
#endif //JETTYCAT_MESSAGE_HPP

/**
 * 聊天消息处理,负责消息持久化到数据库
 *
 */

namespace JettyCat::chat {
using groupId = int;        // 群聊id
using userId = int;         // 用户id


// 消息
class Message {
public:
    enum class Type {
        UNKNOWN,
        TEXT,
        IMAGE,
        VOICE,
        VIDEO,
        FILE
    };

    Message() = default;
    Message(const Message&) = default;
    Message& operator=(const Message& other) = default;

    int load(const nlohmann::json& json);
    int load(const std::string& raw_json);

    inline Message& setContent(const std::string& content) {m_content = content; return *this;}
    inline Message& setType(const Type type) {m_type = type; return *this;}
    inline Message& setType(const std::string& type) {m_type = StringToType(type); return *this;}
    inline Message& setFrom(const userId& from) {m_from = from; return *this;}
    inline Message& setDate(const uint64_t date) {m_date = date; return *this;}

    [[nodiscard]] inline const std::string& getContent() const {return m_content;}
    [[nodiscard]] inline const userId& getFrom() const  {return m_from;}
    [[nodiscard]] inline uint64_t getDate() const  {return m_date;}
    [[nodiscard]] inline Type getType() const {return m_type;}

    static Type StringToType(const std::string& content);
    static std::string TypeToString(Type type);

    [[nodiscard]] std::string dump() const;

private:
    uint64_t m_date{0};             // 时间戳
    userId m_from {-1};                  // 来源
    Type m_type {Type::TEXT};        // 类型
    std::string m_content;          // 根据Type决定消息类型

    // state<->string转换映射
    inline static std::map<std::string, Type> m_STT{
        {"TEXT", Type::TEXT},
        {"IMAGE", Type::IMAGE},
        {"VOICE", Type::VOICE},
        {"VIDEO", Type::VIDEO},
        {"FILE", Type::FILE}
    };
    inline static std::map<Type, std::string> m_TTS{
        {Type::TEXT, "TEXT"},
        {Type::IMAGE, "IMAGE"},
        {Type::VOICE, "VOICE"},
        {Type::VIDEO, "VIDEO"},
        {Type::FILE, "FILE"}
    };
};



// 消息列表
using MessageList = std::list<Message>;


// 数据库访问状态定义
enum class State {
    SUCCESS,
    TIMEOUT,
    FAILED
};

/**
 * @brief 将消息发送到user收件箱,user从此查询自己消息,适用于单点传输
 * @param user_id 要发送给的用户id
 * @param message_list 发送的一个列表数据
 * @return 发送状态
 */
m_sylar::Task<State>sendToUser(const userId& user_id, const MessageList& message_list);                  // 发送到用户收件箱

/**
 *  @brief 将消息存储到群发件箱,群组成员均从此接受消息
 * @param group_id 目标群组id
 * @param message_list 消息列表
 * @return 发送状态
 */
m_sylar::Task<State>sendToGroup(const groupId& group_id, const MessageList& message_list);               // 发送到群聊发件箱,等待其他用户拉取

/**
 * @brief 从目标群收件箱接受群消息, 返回10条消息,(返回的消息条数)通过配置文件配置
 * @param group_id 群组id
 * @param offset 偏移量(翻页)
 * @param message_list 接受到的消息列表
 * @return 接受状态
 */
m_sylar::Task<State>fetchFromGroup(const groupId& group_id, size_t offset, MessageList& message_list);                  // 从群聊接受消息

/**
 * @brief 从当前用户收件箱拉取消息,返回10条消息,(返回的消息条数)通过配置文件配置
 * @param sender_id 消息发送者id(即聊天对方)
 * @param receiver_id 当前用户id,即消息接收者id
 * @param offset 偏移量(翻页)
 * @param message_list 接受到的消息列表
 * @return
 */
m_sylar::Task<State>fetchFromInbox(const userId& sender_id, const userId& receiver_id, size_t offset, MessageList& message_list);            // 从收件箱接受消息

};

