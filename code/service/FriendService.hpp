// FriendService.hpp —— 好友关系的业务逻辑层
//
// 职责：只做"业务校验 + 编排"，不接触 HTTP、不接触 SQL。
//   - 参数合法性校验（可单测）
//   - 调用下层 DAO
//   - 返回统一响应 model（resp::HttpResponse），Controller 不再自己翻译
//
// 全部依赖（FriendDao）通过构造函数注入，所以替换实现 / 单测都容易。
#pragma once

#include "dao/FriendDao.hpp"
#include "http/Response.hpp"

namespace chatter::service {

class FriendService {
public:
    // 依赖注入
    explicit FriendService(std::shared_ptr<dao::FriendDao> friend_dao)
        : m_friend_dao(std::move(friend_dao)) {}

    /**
     * @brief 删除好友的业务逻辑（不含鉴权，鉴权是 Controller 的事）
     * @param self_id 当前登录用户 id（来自 JWT payload）
     * @param friend_id_str 前端传的 "friendId" 字段（原始字符串）
     * @return 统一响应：成功带 msg；失败带错误码与描述
     */
    [[nodiscard]] Task<resp::HttpResponse> removeFriend(
                         JettyCat::chat::userId self_id, const std::string& friend_id_str) const;

    /**
     * @brief 添加好友（校验 friendId + 校验目标存在 + 写入）
     */
    [[nodiscard]] Task<resp::HttpResponse> addFriend(
                         JettyCat::chat::userId self_id, const std::string& friend_id_str) const;

    /**
     * @brief 获取好友列表
     */
    [[nodiscard]] Task<resp::HttpResponse> getFriendList(
                         JettyCat::chat::userId self_id) const;

    /**
     * @brief 查询某个用户的公开信息（昵称/头像/用户名/ID），用于非好友的陌生用户资料展示
     * @param user_id_str 前端传的 "userId" 字段（原始字符串）
     * @return 成功：data.user 含 user_id/username/nickname/avatar_url
     */
    [[nodiscard]] Task<resp::HttpResponse> getPublicProfile(
                         const std::string& user_id_str) const;

private:
    std::shared_ptr<dao::FriendDao> m_friend_dao;
};

}
