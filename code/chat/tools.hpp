#pragma once
#include <string>
#include "init.hpp"

namespace chatWebsocket {
    inline std::string formatUserName(const std::string& username) {
        return getInstanceId() + "_user_" + username;
    }
}

