#pragma once
#include <string>
#include "init.hpp"
#include "Message.hpp"

namespace chatWebsocket {

    // 格式转换到redis存储键
    inline std::string formatUserName(const std::string& user_id) {
        return getInstanceId() + "_user_" + user_id;
    }
    inline std::string formatUserName(const JettyCat::chat::userId& user_id) {
        return getInstanceId() + "_user_" + std::to_string(user_id);
    }
    // 群聊在线成员集合键：{instanceId}_group_{groupId}
    inline std::string formatGroupName(const JettyCat::chat::groupId& group_id) {
        return getInstanceId() + "_group_" + std::to_string(group_id);
    }
}

