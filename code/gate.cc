#include <cstdio>
#include <memory>
#include <iostream>
#include <basic/log.h>
#include <basic/address.h>
#include <http/tcpServer.h>
#include <http/httpServer.h>
#include <basic/config.h>
#include <coroutine/corobase.h>
#include "publicHeader.hpp"
#include "login/register.hpp"

static m_sylar::Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");

void projectInit() {
    // 配置系统初始化
    m_sylar::ConfigManager::LoadJson(projectRoot().string() + "/conf/basic.json", 0);
    m_sylar::ConfigManager::LoadJson(projectRoot().string() + "/config.json", JettyCat_CONFIG_ID);
    // 数据库连接池初始化
    int rt = 1;
    m_sylar::DB::createMysqlPool(10, 25);
    m_sylar::DB::createRedisPool(1, 5);
    rt = m_sylar::DB::Mysql::getInstance()->init("localhost", "koddnty", "73256", "JettyCat", 3306, 0);
    if(rt == -1){
        M_SYLAR_LOG_ERROR(g_logger) << "mysql database pool init failed";
        return;
    }
    rt = m_sylar::DB::Redis::getInstance()->init("localhost", 6379);
    if(rt == -1){
        M_SYLAR_LOG_ERROR(g_logger) << "redis database pool init failed";
        return;
    }
    M_SYLAR_LOG_ERROR(g_logger) << "all database pool init SUCCESS";

}

void urlReg(m_sylar::http::HttpServer::ptr server)
{   
    Register::RegisteUrl(server);
}

int main() {
    m_sylar::IOManager iom("mainIOM", 4);
    m_sylar::http::HttpServer::ptr server(new m_sylar::http::HttpServer(&iom));
    m_sylar::Address::ptr addr = m_sylar::Address::LookupAnyIPAddress("0.0.0.0");
    std::dynamic_pointer_cast<m_sylar::IPv4Address>(addr)->setPort(8803);
    
    projectInit();
    server->bind(addr, 6);

    // url注册
    // 登陆注册等接口
    urlReg(server);

    server->start();

    sleep(1000);
    server->stop();
    iom.autoStop();
    return 0;
}