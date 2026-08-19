#include "FriendService.hpp"

namespace chatter::service {




[[nodiscard]] Task<resp::HttpResponse> FriendService::removeFriend(
                        const JettyCat::chat::userId self_id, const std::string& username_str) const {
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
                        const JettyCat::chat::userId self_id, const std::string& username_str) const {
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

    // ---------- 写入好友关系 ----------
    switch (co_await m_friend_dao->addFriend(self_id, friend_id)) {
    case JettyCat::chat::DBState::SUCCESS:
        http_response.setCode(200).setMsg("Friend added");
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
