#pragma once
#include <basic/log.h>
#include <DBPool/redis.h>
#include <DBPool/mysql.hpp>
#include <DBPool/factory.h>
#include <server/http/httpServer.hpp>
#include <coroutine/corobase.h>
#include <filesystem>
#include <unistd.h>
#include <protocol/http/http.hpp>

#define JettyCat_CONFIG_ID 1

using namespace m_sylar;



static std::filesystem::path projectRoot() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if(len <= 0) {
        return std::filesystem::current_path();
    }
    buf[len] = '\0';
    std::filesystem::path exePath(buf);
    return exePath.parent_path().parent_path();
}