#pragma once
#include <basic/log.h>
#include <DBPool/redis.h>
#include <DBPool/mysql.h>
#include <DBPool/factory.h>
#include <http/httpServer.h>
#include <coroutine/corobase.h>
#include <filesystem>
#include <unistd.h>

#define JettyCat_CONFIG_ID 1




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