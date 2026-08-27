// FileService.hpp —— 文件资源的业务逻辑层
//
// 职责：鉴权信息校验 + 资源路径权限规则 + 调 DAO 申请凭证 + 组装响应。
// 不接触 HTTP/WS 协议细节。全部依赖（FileDao / FriendDao / GroupDao）通过构造函数注入。
#pragma once

#include "files/dao/FileDao.hpp"
#include "dao/FriendDao.hpp"
#include "dao/GroupDao.hpp"
#include "http/Response.hpp"
#include "login/tools.hpp"

namespace JettyCat::file::service {

class FileService {
public:
    explicit FileService(std::shared_ptr<dao::FileDao> file_dao,
                         std::shared_ptr<chatter::dao::FriendDao> friend_dao,
                         std::shared_ptr<chatter::dao::GroupDao> group_dao)
        : m_file_dao(std::move(file_dao)),
          m_friend_dao(std::move(friend_dao)),
          m_group_dao(std::move(group_dao)) {}

    /**
     * @brief 申请上传凭证（POST /files/upload/sts）
     *
     * 只给当前用户"写一个对象"的权限：对象 key = <目标snow_id>/<文件hash>。
     * 目标由消息产生地决定：
     *   - 私聊(user)：目标 = 接收者 user_snow_id
     *   - 群聊(group)：目标 = 群聊 group_snow_id
     * 需校验目标确实是当前用户的好友/已加入的群，防止越权上传到他人目录。
     *
     * @param payload       已解析的 JWT payload
     * @param target_kind   "user" | "group"
     * @param target_id_str 目标 snow id（原始字符串）
     * @param file_hash     文件 hash（对象 key 的一部分）
     * @param duration_str  凭证时长（秒，可选，默认 300=5min）
     * @return 统一 HTTP 响应，data 含 objectKey + 写凭证
     */
    [[nodiscard]] m_sylar::Task<chatter::resp::HttpResponse> getUploadSts(
        const JWT::Payload& payload,
        const std::string& target_kind,
        const std::string& target_id_str,
        const std::string& file_hash,
        const std::string& duration_str) const;

    /**
     * @brief 申请获取凭证（POST /files/fetch/sts）
     *
     * 把当前用户"所有可读位置"的读权限写进策略：
     *   - 自己的目录 <自己>/    （他人发给我的图片）
     *   - 每个好友的目录 <好友>/（我发给好友的图片）
     *   - 每个已加入群聊的目录 <群>/（群聊里的图片）
     * 凭证默认 3 小时，可配置（配置项 fetch.stsDurationSeconds）。
     *
     * @param payload       已解析的 JWT payload
     * @param duration_str  凭证时长（秒，可选）
     * @return 统一 HTTP 响应，data 含 readablePaths + 读凭证
     */
    [[nodiscard]] m_sylar::Task<chatter::resp::HttpResponse> getFetchSts(
        const JWT::Payload& payload,
        const std::string& duration_str) const;

private:
    std::shared_ptr<dao::FileDao> m_file_dao;
    std::shared_ptr<chatter::dao::FriendDao> m_friend_dao;
    std::shared_ptr<chatter::dao::GroupDao> m_group_dao;
};

} // namespace JettyCat::file::service