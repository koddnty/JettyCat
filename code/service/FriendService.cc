#include "FriendService.hpp"

namespace chatter::service {




[[nodiscard]] Task<resp::HttpResponse> FriendService::removeFriend(
                        const JettyCat::chat::userId self_id, const std::string& friend_id_str) const {
    resp::HttpResponse http_response;

    // ---------- 参数校验 ----------
    if (friend_id_str.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'friendId'");
        co_return http_response;
    }

    JettyCat::chat::userId friend_id = 0;
    bool parse_error = false;
    try {
        friend_id = std::stoi(friend_id_str);
    } catch (const std::exception&) {
        parse_error = true;
    }
    if (parse_error || friend_id <= 0 || friend_id == self_id) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Invalid 'friendId'");
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
                        const JettyCat::chat::userId self_id, const std::string& friend_id_str) const {
    resp::HttpResponse http_response;

    // ---------- 参数校验 ----------
    if (friend_id_str.empty()) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Missing or invalid 'friendId'");
        co_return http_response;
    }

    JettyCat::chat::userId friend_id = 0;
    bool parse_error = false;
    try {
        friend_id = std::stoi(friend_id_str);
    } catch (const std::exception&) {
        parse_error = true;
    }
    if (parse_error) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Invalid 'friendId'");
        co_return http_response;
    }
    if (friend_id <= 0) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Invalid 'friendId'");
        co_return http_response;
    }
    if (friend_id == self_id) {
        http_response.setCode(400).setMsg("BAD_REQUEST: Cannot add yourself as friend");
        co_return http_response;
    }

    // ---------- 校验目标用户存在 ----------
    bool exists = false;
    switch (co_await m_friend_dao->userExists(friend_id, exists)) {
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
        http_response.setCode(404).setMsg("NOT_FOUND: friendId does not exist");
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
}
