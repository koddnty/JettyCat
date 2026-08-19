// GroupService.hpp —— 群组关系的业务逻辑层
//
// 职责：只做"业务校验 + 编排"，不接触 HTTP、不接触 SQL。
// 全部依赖（GroupDao）通过构造函数注入。
#pragma once

#include "dao/GroupDao.hpp"
#include "http/Response.hpp"

namespace chatter::service {

class GroupService {
public:
    explicit GroupService(std::shared_ptr<dao::GroupDao> group_dao)
        : m_group_dao(std::move(group_dao)) {}

    /**
     * @brief 加入群聊（按对外 group_id 查询群组, 得到对内 group_snow_id 后写入）
     * @param self_id 当前登录用户的 user_snow_id (来自 JWT)
     * @param group_id_str 前端传的对外 "groupId" 字段(原始字符串)
     */
    [[nodiscard]] Task<resp::HttpResponse> addGroup(
                         JettyCat::chat::userId self_id, const std::string& group_id_str) const;

    /**
     * @brief 退出群聊（按对外 group_id 定位群组）
     */
    [[nodiscard]] Task<resp::HttpResponse> removeGroup(
                         JettyCat::chat::userId self_id, const std::string& group_id_str) const;

    /**
     * @brief 获取加入的群聊列表
     */
    [[nodiscard]] Task<resp::HttpResponse> getGroupList(
                         JettyCat::chat::userId self_id) const;

private:
    std::shared_ptr<dao::GroupDao> m_group_dao;
};

} // namespace chatter::service
