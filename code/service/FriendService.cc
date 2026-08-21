#include "FriendService.hpp"

namespace chatter::service {




[[nodiscard]] Task<JettyCat::chat::DBState> FriendService::resolveTargetId(
                        const std::string& key, JettyCat::chat::userId& out_id, bool& exists) const {
    out_id = 0;
    exists = false;
    if (key.empty()) {
        co_return JettyCat::chat::DBState::SUCCESS;   // exists 保持 false
    }
    // 纯数字 → 视为对内 user_snow_id，直接使用（非法数字视为不存在）
    if (key.find_first_not_of("0123456789") == std::string::npos) {
        try {
            out_id = std::stoll(key);
        } catch (const std::exception&) {
            out_id = 0;
        }
        exists = out_id > 0;
        co_return JettyCat::chat::DBState::SUCCESS;
    }
    // 否则按对外账号(users.user_id)查库
    co_return co_await m_friend_dao->getUserSnowIdByUserId(key, out_id, exists);
}


[[nodiscard]] Task<resp::HttpResponse> FriendService::removeFriend(
                        const JettyCat::chat::userId self_id, const std::string& target_key) const {
    resp::HttpResponse http_response;

    // ---------- 参数校验 ----------
    if (target_key.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'username'");
        co_return http_response;
    }

    // ---------- 解析目标用户 (账号 或 纯数字对内 id) ----------
    JettyCat::chat::userId friend_id = 0;
    bool exists = false;
    switch (co_await resolveTargetId(target_key, friend_id, exists)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }
    if (!exists) {
        http_response.setCode(404).setMsg("NOT_FOUND: user does not exist");
        co_return http_response;
    }
    if (friend_id == self_id) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Cannot operate on yourself");
        co_return http_response;
    }

    // ---------- 调用 DAO ----------
    switch (co_await m_friend_dao->removeFriend(self_id, friend_id)) {
    case JettyCat::chat::DBState::SUCCESS:
        http_response.setCode(200).setMsg("Friend removed");
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        break;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        break;
    }
    co_return http_response;
}


[[nodiscard]] Task<resp::HttpResponse> FriendService::addFriend(
                        const JettyCat::chat::userId self_id, const std::string& username_str,
                        nlohmann::json& data_out) const {
    resp::HttpResponse http_response;

    // ---------- 参数校验 ----------
    if (username_str.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'username'");
        co_return http_response;
    }

    // ---------- 按账号(用户名)查找目标用户 ----------
    JettyCat::chat::userId friend_id = 0;
    bool exists = false;
    switch (co_await m_friend_dao->getUserSnowIdByUserId(username_str, friend_id, exists)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }
    if (!exists) {
        http_response.setCode(404).setMsg("NOT_FOUND: user does not exist");
        co_return http_response;
    }
    if (friend_id == self_id) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Cannot add yourself as friend");
        co_return http_response;
    }

    // ---------- 查询当前关系状态 ----------
    int cur_status = -1;
    bool rel_exists = false;
    switch (co_await m_friend_dao->getFriendStatus(self_id, friend_id, cur_status, rel_exists)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }

    if (rel_exists && cur_status == static_cast<int>(dao::FriendStatus::NORMAL)) {
        http_response.setCode(400).setMsg("ALREADY_FRIENDS: Already friends");
        co_return http_response;
    }
    if (rel_exists && cur_status != static_cast<int>(dao::FriendStatus::BLOCKED)) {
        // 已是待确认(2/3), 说明对方或自己已发出过申请, 等待确认即可
        http_response.setCode(400).setMsg("REQUEST_PENDING: Friend request already pending");
        co_return http_response;
    }

    // ---------- 写入待确认好友申请 ----------
    switch (co_await m_friend_dao->sendFriendRequest(self_id, friend_id)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }

    // ---------- 组装申请发起方(自己)的公开信息, 供 WS 推送与前端展示 ----------
    nlohmann::json profile;
    bool prof_exists = false;
    switch (co_await m_friend_dao->getPublicProfile(self_id, prof_exists, profile)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }
    if (!prof_exists) {
        http_response.setCode(500).setMsg("Internal server error");
        co_return http_response;
    }

    data_out["friend_id"] = std::to_string(friend_id);   // 供 Controller 做 WS 同步推送(目标被申请方)
    data_out["requester"] = profile;
    nlohmann::json data;
    data["friend_id"] = std::to_string(friend_id);
    data["requester"] = profile;
    http_response.setCode(200).setMsg("Friend request sent").setData(data);
    co_return http_response;
}


[[nodiscard]] Task<resp::HttpResponse> FriendService::agreeFriend(
                        const JettyCat::chat::userId self_id, const std::string& target_key,
                        nlohmann::json& data_out) const {
    resp::HttpResponse http_response;

    // ---------- 参数校验 ----------
    if (target_key.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'username'");
        co_return http_response;
    }

    // ---------- 解析申请方 (账号 或 纯数字对内 id) ----------
    JettyCat::chat::userId friend_id = 0;
    bool exists = false;
    switch (co_await resolveTargetId(target_key, friend_id, exists)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }
    if (!exists) {
        http_response.setCode(404).setMsg("NOT_FOUND: user does not exist");
        co_return http_response;
    }
    if (friend_id == self_id) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Cannot agree with yourself");
        co_return http_response;
    }

    // ---------- 校验存在待确认申请 ----------
    int cur_status = -1;
    bool rel_exists = false;
    switch (co_await m_friend_dao->getFriendStatus(self_id, friend_id, cur_status, rel_exists)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }
    if (!rel_exists ||
        (cur_status != static_cast<int>(dao::FriendStatus::PENDING_SMALL_LARGE) &&
         cur_status != static_cast<int>(dao::FriendStatus::PENDING_LARGE_SMALL))) {
        http_response.setCode(404).setMsg("NOT_FOUND: No pending friend request");
        co_return http_response;
    }

    // ---------- 同意申请: 置为正常好友 ----------
    switch (co_await m_friend_dao->agreeFriend(self_id, friend_id)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }

    // ---------- 组装被同意方(申请方)的公开信息, 供 WS 推送与前端展示 ----------
    nlohmann::json profile;
    bool prof_exists = false;
    switch (co_await m_friend_dao->getPublicProfile(friend_id, prof_exists, profile)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }

    data_out["friend"] = profile;
    nlohmann::json data;
    data["friend"] = profile;
    http_response.setCode(200).setMsg("Friend request agreed").setData(data);
    co_return http_response;
}


[[nodiscard]] Task<resp::HttpResponse> FriendService::getFriendRequests(
                        const JettyCat::chat::userId self_id) const {
    resp::HttpResponse http_response;

    nlohmann::json requests = nlohmann::json::array();
    switch (co_await m_friend_dao->listFriendRequests(self_id, requests)) {
    case JettyCat::chat::DBState::SUCCESS: {
        nlohmann::json data;
        data["requests"] = requests;
        data["count"]    = requests.size();
        http_response.setCode(200).setMsg("ok").setData(data);
        break;
    }
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        break;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        break;
    }
    co_return http_response;
}


[[nodiscard]] Task<resp::HttpResponse> FriendService::getFriendList(
                        const JettyCat::chat::userId self_id) const {
    resp::HttpResponse http_response;

    nlohmann::json friends = nlohmann::json::array();
    switch (co_await m_friend_dao->listFriends(self_id, friends)) {
    case JettyCat::chat::DBState::SUCCESS: {
        nlohmann::json data;
        data["friends"] = friends;
        data["count"]   = friends.size();
        http_response.setCode(200).setMsg("ok").setData(data);
        break;
    }
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        break;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        break;
    }
    co_return http_response;
}


[[nodiscard]] Task<resp::HttpResponse> FriendService::getPublicProfile(
                        const std::string& user_id_str) const {
    resp::HttpResponse http_response;

    // ---------- 参数校验 ----------
    if (user_id_str.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'userId'");
        co_return http_response;
    }

    // 兼容两种查询方式: 纯数字按 user_id 查, 否则按账号(用户名)查
    JettyCat::chat::userId user_id = 0;
    bool exists = false;
    if (user_id_str.find_first_not_of("0123456789") == std::string::npos) {
        bool parse_error = false;
        try {
            user_id = std::stoll(user_id_str);
        } catch (const std::exception&) {
            parse_error = true;
        }
        if (parse_error || user_id <= 0) {
            http_response.setCode(400).setMsg("BAD_REQUEST: Invalid 'userId'");
            co_return http_response;
        }

        // ---------- 调用 DAO ----------
        nlohmann::json profile;
        switch (co_await m_friend_dao->getPublicProfile(user_id, exists, profile)) {
        case JettyCat::chat::DBState::SUCCESS:
            break;
        case JettyCat::chat::DBState::TIMEOUT:
            http_response.setCode(500).setMsg("Database query timeout");
            co_return http_response;
        default:
            http_response.setCode(500).setMsg("Database query failed");
            co_return http_response;
        }
    } else {
        // 按用户名查询
        switch (co_await m_friend_dao->getUserSnowIdByUserId(user_id_str, user_id, exists)) {
        case JettyCat::chat::DBState::SUCCESS:
            break;
        case JettyCat::chat::DBState::TIMEOUT:
            http_response.setCode(500).setMsg("Database query timeout");
            co_return http_response;
        default:
            http_response.setCode(500).setMsg("Database query failed");
            co_return http_response;
        }
    }
    if (!exists) {
        http_response.setCode(404).setMsg("NOT_FOUND: user does not exist");
        co_return http_response;
    }

    // 按 id 获取公开资料
    nlohmann::json profile;
    switch (co_await m_friend_dao->getPublicProfile(user_id, exists, profile)) {
    case JettyCat::chat::DBState::SUCCESS:
        break;
    case JettyCat::chat::DBState::TIMEOUT:
        http_response.setCode(500).setMsg("Database query timeout");
        co_return http_response;
    default:
        http_response.setCode(500).setMsg("Database query failed");
        co_return http_response;
    }
    if (!exists) {
        http_response.setCode(404).setMsg("NOT_FOUND: user does not exist");
        co_return http_response;
    }

    nlohmann::json data;
    data["user"] = profile;
    http_response.setCode(200).setMsg("ok").setData(data);
    co_return http_response;
}
} // namespace chatter::service}
