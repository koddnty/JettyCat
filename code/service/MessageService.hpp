// MessageService.hpp —— 消息查询的业务逻辑层
//
// 职责：参数校验 + 权限规则 + 调 DAO + 组装响应，不接触 HTTP/WS 协议细节。
// 全部依赖（MessageDao）通过构造函数注入。
#pragma once

#include "dao/MessageDao.hpp"
#include "http/Response.hpp"

namespace chatter::service {

class MessageService {
public:
    explicit MessageService(std::shared_ptr<dao::MessageDao> message_dao)
        : m_message_dao(std::move(message_dao)) {}

    /**
     * @brief 拉取私聊消息（含 receiverId 越权规则）
     * @param jwt_user_id 当前登录用户 id（来自 JWT payload）
     * @param sender_id_str 请求参数 senderId（原始字符串，必填）
     * @param receiver_id_str 请求参数 receiverId（原始字符串，可选）
     * @param offset_str 请求参数 offset（原始字符串，可选）
     */
    [[nodiscard]] Task<resp::HttpResponse> fetchUserMessage(
                         JettyCat::chat::userId jwt_user_id,
                         const std::string& sender_id_str,
                         const std::string& receiver_id_str,
                         const std::string& offset_str) const;

    /**
     * @brief 拉取群聊消息
     * @param group_id_str 请求参数 groupId（原始字符串，必填）
     * @param offset_str 请求参数 offset（原始字符串，可选）
     */
    [[nodiscard]] Task<resp::HttpResponse> fetchGroupMessage(
                         const std::string& group_id_str,
                         const std::string& offset_str) const;

private:
    std::shared_ptr<dao::MessageDao> m_message_dao;
};

} // namespace chatter::service
