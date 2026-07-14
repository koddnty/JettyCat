#include <memory>
#include <csignal>
#include <iostream>
#include <basic/log.h>
#include <basic/address.h>
#include <server/http/httpServer.hpp>
#include <basic/config.h>
#include <coroutine/corobase.h>
#include "publicHeader.hpp"
#include "login/register.hpp"
#include <sylar/server/websocket/wsserver.hpp>
#include "chat/connection.hpp"

static m_sylar::Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");
static sem_t g_sem;


void signalHandler(int signum) {
    sem_post(&g_sem);
}

void projectInit() {
    //  程序初始化
    sem_init(&g_sem, 0, 0);
    signal(SIGINT, signalHandler);

    // 配置系统初始化
    m_sylar::ConfigManager::LoadJson(projectRoot().string() + "/conf/basic.json", 0);
    m_sylar::ConfigManager::LoadJson(projectRoot().string() + "/conf/jettyCat.json", JettyCat_CONFIG_ID);

    // 数据库连接池初始化
    m_sylar::ConfigVar<std::string>::ptr mysql_host =
            m_sylar::ConfigManager::LookUp("mysql.host", std::string("localhost"), JettyCat_CONFIG_ID, "mysql host");
    m_sylar::ConfigVar<int>::ptr mysql_port =
            m_sylar::ConfigManager::LookUp("mysql.port", 3306, JettyCat_CONFIG_ID, "mysql port");
    m_sylar::ConfigVar<std::string>::ptr mysql_user =
            m_sylar::ConfigManager::LookUp("mysql.user", std::string(""), JettyCat_CONFIG_ID, "mysql user");
    m_sylar::ConfigVar<std::string>::ptr mysql_passwd =
            m_sylar::ConfigManager::LookUp("mysql.password", std::string(""), JettyCat_CONFIG_ID, "mysql passwd");
    m_sylar::ConfigVar<std::string>::ptr mysql_database =
            m_sylar::ConfigManager::LookUp("mysql.database", std::string(""), JettyCat_CONFIG_ID, "mysql database");

    m_sylar::ConfigVar<int>::ptr mysql_min_connection_num =
            m_sylar::ConfigManager::LookUp("mysql.minConnectionNum", 10, JettyCat_CONFIG_ID, "mysql pool min connection num");
    m_sylar::ConfigVar<int>::ptr mysql_max_connection_num =
            m_sylar::ConfigManager::LookUp("mysql.maxConnectionNum", 20, JettyCat_CONFIG_ID, "mysql pool max connection num");

    m_sylar::ConfigVar<std::string>::ptr redis_host =
            m_sylar::ConfigManager::LookUp("redis.host", std::string("localhost"), JettyCat_CONFIG_ID, "redis host");
    m_sylar::ConfigVar<int>::ptr redis_port =
            m_sylar::ConfigManager::LookUp("redis.port", 6379, JettyCat_CONFIG_ID, "redis port");
    
    m_sylar::ConfigVar<int>::ptr redis_min_connection_num =
            m_sylar::ConfigManager::LookUp("redis.minConnectionNum", 5, JettyCat_CONFIG_ID, "redis pool min connection num");
    m_sylar::ConfigVar<int>::ptr redis_max_connection_num =
            m_sylar::ConfigManager::LookUp("redis.maxConnectionNum", 10, JettyCat_CONFIG_ID, "redis pool max connection num");



    int rt = 1;
    m_sylar::DB::createMysqlPool(mysql_min_connection_num->getValue(), mysql_max_connection_num->getValue());
    m_sylar::DB::createRedisPool(redis_min_connection_num->getValue(), redis_max_connection_num->getValue());
    M_SYLAR_LOG_INFO(g_logger) << "mysql host: " << mysql_host->getValue() << ", port: " << mysql_port->getValue() 
        << ", user: " << mysql_user->getValue() << ", database: " << mysql_database->getValue();
    rt = m_sylar::DB::Mysql::getInstance()->init(mysql_host->getValue(), mysql_user->getValue(), mysql_passwd->getValue(), mysql_database->getValue(), mysql_port->getValue(), 0);

    if(rt == -1){
        M_SYLAR_LOG_ERROR(g_logger) << "mysql database pool init failed";
        return;
    }
    rt = m_sylar::DB::Redis::getInstance()->init(redis_host->getValue(), redis_port->getValue());
    if(rt == -1){
        M_SYLAR_LOG_ERROR(g_logger) << "redis database pool init failed";
        return;
    }
    M_SYLAR_LOG_INFO(g_logger) << "all database pool init SUCCESS";
}

void projectCleanUp(m_sylar::IOManager& iom, m_sylar::http::HttpServer::ptr server) {
    // 程序清理
    sem_wait(&g_sem);
    M_SYLAR_LOG_INFO(g_logger) << "Program existing...";
    server->stop();
    iom.autoStop();
    M_SYLAR_LOG_INFO(g_logger) << "Program exited successed";
    return ;
}

void urlReg(m_sylar::http::HttpServer::ptr http_server, m_sylar::websocket::WsServer::ptr ws_server){
    Register::registeUrl(http_server);
    ChatWebSocketServer::registeUrl(ws_server);
}



int main() {
    m_sylar::IOManager iom("jettyCat", 4);
    m_sylar::http::HttpServer::ptr http_server(new m_sylar::http::HttpServer(&iom));
    m_sylar::Address::ptr addr = m_sylar::Address::LookupAnyIPAddress("0.0.0.0");
    std::dynamic_pointer_cast<m_sylar::IPv4Address>(addr)->setPort(8803);

    m_sylar::websocket::WsServer::ptr ws_server = std::make_shared<m_sylar::websocket::WsServer>(http_server);
    
    projectInit();
    http_server->bind(addr, 6);

    // url注册
    // 登陆注册等接口
    urlReg(http_server, ws_server);

    http_server->start();

    projectCleanUp(iom, http_server);
    return 0;
}