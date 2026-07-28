#pragma once
#include <string>
#include "init.hpp"
#include "MessageList.hpp"

namespace chatWebsocket {
    inline std::string formatUserName(const std::string& user_id) {
        return getInstanceId() + "_user_" + user_id;
    }
    inline std::string formatUserName(const JettyCat::chat::userId& user_id) {
        return getInstanceId() + "_user_" + std::to_string(user_id);
    }
}

