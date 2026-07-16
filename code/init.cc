#include "init.hpp"
static m_sylar::Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");


// 服务端实例id
const std::string& getInstanceId() {
    static std::string s_instance_id = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
        ).count());
    return s_instance_id;
}


/**
 * 程序初始化与退出处理
*/
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
}
