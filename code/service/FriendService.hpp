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
     * @param target_key 前端传的目标用户标识：兼容账号(username)或纯数字对内 user_snow_id(friendId)
     * @return 统一响应：成功带 msg；失败带错误码与描述
     */
    [[nodiscard]] Task<resp::HttpResponse> removeFriend(
                         JettyCat::chat::userId self_id, const std::string& target_key) const;

    /**
     * @brief 添加好友（按 username 查找目标用户 + 校验 + 写入待确认申请）
     *        成功后由 Controller 负责通过 WebSocket 同步通知对方。
     * @param data_out 输出：成功时含申请发起方的公开信息(requester 字段)，供 WS 推送与前端展示
     */
    [[nodiscard]] Task<resp::HttpResponse> addFriend(
                         JettyCat::chat::userId self_id, const std::string& username_str,
                         nlohmann::json& data_out) const;

    /**
     * @brief 同意好友申请（对方发起，当前用户确认），将关系置为正常。
     *        成功后由 Controller 负责通过 WebSocket 同步通知申请方。
     * @param target_key 目标(申请方)用户标识：兼容账号(username)或纯数字对内 user_snow_id(friendId)
     * @param data_out 输出：成功时含被同意方的公开信息(friend 字段)，供 WS 推送与前端展示
     */
    [[nodiscard]] Task<resp::HttpResponse> agreeFriend(
                         JettyCat::chat::userId self_id, const std::string& target_key,
                         nlohmann::json& data_out) const;

    /**
     * @brief 查询发给当前用户的待确认好友申请列表
     */
    [[nodiscard]] Task<resp::HttpResponse> getFriendRequests(
                         JettyCat::chat::userId self_id) const;

    /**
     * @brief 获取好友列表
     */
    [[nodiscard]] Task<resp::HttpResponse> getFriendList(
                         JettyCat::chat::userId self_id) const;

    /**
     * @brief 查询某个用户的公开信息（昵称/头像/用户名/ID）
     * @param user_id_str 前端传的 "userId" 字段（原始字符串）; 兼容按 username 查询
     * @return 成功：data.user 含 user_id/username/nickname/avatar_url
     */
    [[nodiscard]] Task<resp::HttpResponse> getPublicProfile(
                         const std::string& user_id_str) const;

private:
    std::shared_ptr<dao::FriendDao> m_friend_dao;

    /**
     * @brief 将目标标识(账号 或 纯数字对内 id)解析为对内 user_snow_id
     * @param key 目标标识：纯数字视为对内 user_snow_id，否则按账号(user_id)查库
     * @param exists 输出：目标是否有效(数字>0 或 查到账号)
     */
    [[nodiscard]] Task<JettyCat::chat::DBState> resolveTargetId(
                         const std::string& key, JettyCat::chat::userId& out_id, bool& exists) const;
};

}
