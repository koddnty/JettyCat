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
     * @brief 加入群聊（校验 groupId + 校验目标群聊存在 + 写入）
     */
    [[nodiscard]] Task<resp::HttpResponse> addGroup(
                         JettyCat::chat::userId self_id, const std::string& group_id_str) const;

    /**
     * @brief 退出群聊
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
