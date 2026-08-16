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
}
