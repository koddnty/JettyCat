#pragma once
#include "publicHeader.hpp"
#include "tools.hpp"
#include <nlohmann/json.hpp>


class Register
{
public:
    Register() {}
    ~Register() {}



    // interface
public:
    static void registeUrl(m_sylar::http::HttpServer::ptr server);

    // 测试连通性 
    static m_sylar::Task<void> test(m_sylar::http::HttpSession::ptr session) {
        std::cout << "HelloWrold!" << std::endl;
        session->getResponse()->setBody("HelloWrold!");
        co_await session->co_sendResp();
        co_return;
    }

    // 注册
    // 获取注册验证码
    static m_sylar::Task<void> coGetRegCode(m_sylar::http::HttpSession::ptr session);
    
    static m_sylar::Task<void> registe(m_sylar::http::HttpSession::ptr session);
    // 登陆
    // 生成JWT
    static std::string generateJWT(const std::string& username, RolePermissions::Role role);

    static bool verifyJWT(m_sylar::http::HttpSession::ptr session);

    // 登陆接口
    static m_sylar::Task<void> coLogin(m_sylar::http::  HttpSession::ptr session);


    // 验证接口
    // JWT-权限验证接口
    static m_sylar::Task<void> coVerifyJWT(const std::string& token);
};

