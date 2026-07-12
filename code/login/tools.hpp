#pragma once
#include <string.h>
#include <iostream>
#include <server/http/httpServer.hpp>
#include <nlohmann/json.hpp>
#include <basic/log.h>
#include "publicHeader.hpp"

class RolePermissions {
public:
    RolePermissions() {}
    ~RolePermissions() {}

    enum Role : unsigned int{
        UNKNOWN = 0,
        USER = 1 << 0,
        ADMIN = 1 << 1,
    };

    static std::string RoleToString(Role role);
    static Role RoleFromString(std::string roleStr);


};


class JWT {
public:
    JWT() {}
    ~JWT() {}

    class Header {
    public:
        std::string typ = "";
        std::string alg = "";
    };

    class Payload {
    public:
        std::string username = "";
        RolePermissions::Role role = RolePermissions::Role::UNKNOWN;
    };

    static std::string generateJWT(const std::string& username, RolePermissions::Role role);
    static bool verifyJWT(m_sylar::http::HttpSession::ptr session);
    static bool verifyJWT(const std::string& jwt);

    static JWT::Header parserHeader(const std::string& jwt);
    static JWT::Payload parserPayload(const std::string& jwt);

    
};


class Encode {
public:
    Encode() {}
    ~Encode() {}

    // 工具函数
    static std::string base64JWTEncode(const std::string &input);
    static std::string base64JWTDecode(const std::string& input);
    static std::string base64Encode(const std::string &input);
    static std::string base64Decode(const std::string& input);
};


class Hash {
public:
    Hash() {}
    ~Hash() {}



    // 密码加密
    // 使用 PBKDF2 进行密码哈希，生成随机盐并返回盐和哈希的组合
    static int hashPBKDF2(const std::string& pw, std::string& passwd, std::string& salt_out);
    static int hashPBKDF2WithSalt(const std::string &pw, std::string &passwd, const std::string &salt_out);
    static int generatePassword(const std::string& password, std::string& password_hash, std::string& salt);
    static bool verifyPassword(const std::string& password, const std::string& password_hash);
};


class TemplateHeader {
public:
    TemplateHeader() {}
    ~TemplateHeader() {}

    // OPTION请求返回false. 其他请求返回true.
    static bool CORSALL(m_sylar::http::HttpSession::ptr session);  
};


