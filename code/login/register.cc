#include "register.hpp"
#include <cstdlib>   
#include <ctime>    

static m_sylar::Logger::ptr j_logger = M_SYLAR_LOG_NAME("jettyCat");
using namespace m_sylar;









// interface
void Register::RegisteUrl(m_sylar::http::HttpServer::ptr server) {
    srand((unsigned)time(NULL));
    if(server == nullptr)
    {
        M_SYLAR_LOG_ERROR(j_logger) << "invalid http server, http server is nullptr";
        return;
    }
    server->GET("/test", Register::test);
    server->GET("/registe/getRegCode", Register::coGetRegCode);
    server->GET("/login/jwt", Register::coLogin);
    server->GET("/registe/registe", Register::registe);
}


/**
    @brief 获取注册验证码
        生成redis验证码，key为reg_code_时间戳，value为8位随机数，过期时间为时间戳
        time_limit：验证码过期时间，单位为秒，前端管理员传入。
*/
m_sylar::Task<void> Register::coGetRegCode(m_sylar::http::HttpSession::ptr session) {
    http::HttpRequest::ptr req = session->getRequest();
    http::HttpResponse::ptr resp = session->getResponse();
    // 验证身份
    M_SYLAR_LOG_INFO(j_logger) << req->getHeader("Cookie", "");
    M_SYLAR_LOG_INFO(j_logger) <<  req->getCookie("Path", "");
    M_SYLAR_LOG_INFO(j_logger) <<  req->getCookie("jwttoken", "");

 
    // 获取请求参数
    std::string time_limit = "time-limit"; 
    std::string valid_times = "valid-times"; 
    time_limit = req->getParam(time_limit); 
    valid_times = req->getParam(valid_times);
    M_SYLAR_LOG_INFO(j_logger) << "generate one registe code" << std::endl;

    // 生成验证码并存入redis
    int num = rand() % 90000000 + 10000000;
    std::string cmd = "set reg_code_" + std::to_string(num) + " " + valid_times + " EX " + time_limit;
    RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    // 响应
    if(reply->getState() == IOState::SUCCESS && reply->asString() == "OK"){
        resp->setBody(std::to_string(num));
    }
    else if(reply->getState() == IOState::TIMEOUT){
        resp->setBody("timeout");
    }
    co_return;
}


// 注册
m_sylar::Task<void> Register::registe(m_sylar::http::HttpSession::ptr session) {
    http::HttpRequest::ptr req = session->getRequest();
    http::HttpResponse::ptr resp = session->getResponse();
    // 获取请求参数
    std::string username = "username";
    std::string password = "password";
    std::string reg_code = "reg_code";
    username = req->getParam(username);
    password = req->getParam(password);
    reg_code = req->getParam(reg_code);

    // 参数验证
    if(username.empty() || password.empty() || reg_code.empty()) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Missing required parameters";
        resp->setBody(j.dump());
        resp->setStatus(http::HttpStatus::BAD_REQUEST);
        co_return;
    }
    if(username.size() > 32 || password.size() > 32) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Username or password too long";
        resp->setBody(j.dump());
        resp->setStatus(http::HttpStatus::BAD_REQUEST);
        co_return;
    }
    if(username.size() < 3 || password.size() < 6) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Username must be at least 3 characters and password must be at least 6 characters";
        resp->setBody(j.dump());
        resp->setStatus(http::HttpStatus::BAD_REQUEST);
        co_return;
    }

    // 验证验证码
    std::string cmd = "DECR reg_code_" + reg_code;
    RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    M_SYLAR_LOG_INFO(j_logger) << "redis reply" << reply->asString() << "  " << reply->asInt();
    if(reply->getState() != IOState::SUCCESS || reply->asInt() <= -1){
        reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery("DEL reg_code_" + reg_code); // 删除过期或无效的验证码
        if(reply->getState() != IOState::SUCCESS){
            M_SYLAR_LOG_ERROR(j_logger) << "Failed to delete invalid reg code: " << reg_code;
        }
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Invalid registration code";
        resp->setBody(j.dump());
        resp->setStatus(http::HttpStatus::BAD_REQUEST);
        co_return;
    }

    // 哈希密码并生成盐
    std::string hashed_password, salt;
    if(Hash::hashPBKDF2(password, hashed_password, salt) == -1) {
        M_SYLAR_LOG_ERROR(j_logger) << "Password hashing failed";
        nlohmann::json j;
        j["status"] = "error";
        j["error"] = "internal server error";
        resp->setBody(j.dump());
        resp->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
        co_return;
    }
    
    std::string password_hash = Encode::base64Encode(hashed_password) + "," + Encode::base64Encode(salt); // 存储哈希值，盐单独存储

    // 插入
    // std::cout << "hashed_password: " << hashed_password << std::endl;
    std::string insert_user_query = "INSERT INTO users (username, password_hash, role) VALUES ('" + username + "', '" + password_hash + "', 'USER')";
    MySQLResp::ptr insert_user_resp = co_await m_sylar::DB::Mysql::getInstance()->executeQuery(
        insert_user_query
    );
    M_SYLAR_LOG_INFO(j_logger) << insert_user_query;
    
    if(insert_user_resp->getState() != IOState::SUCCESS) {
        nlohmann::json j;
        if(insert_user_resp->getState() == IOState::TIMEOUT) {
            j["status"] = "error";
            j["error"] = "Internal Server Error";
            resp->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
        } else {
            j["status"] = "failed";
            j["error"] = "Failed to register user, possibly due to duplicate username";
            resp->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
        }
        resp->setBody(j.dump());
        co_return;
    }

    // 构建jwt并响应
    std::string jwt = JWT::generateJWT(username, RolePermissions::USER);
    nlohmann::json j;
    j["status"] = "success";

    std::string cookie = "jwttoken=" + jwt + "; HttpOnly; Secure; SameSite=Strict; Path=/";
    resp->setHeader("Set-Cookie", cookie);
    resp->setBody(j.dump());
    co_return;
}


// 登陆
m_sylar::Task<void> Register::coLogin(m_sylar::http::HttpSession::ptr session) {
    http::HttpRequest::ptr req = session->getRequest();
    http::HttpResponse::ptr resp = session->getResponse();
    // 获取请求参数
    std::string username = "username";
    std::string password = "password";
    username = req->getParam(username);
    password = req->getParam(password);

    std::string hashed_password, salt;
    if(Hash::hashPBKDF2(password, hashed_password, salt) == -1) {
        M_SYLAR_LOG_ERROR(j_logger) << "Password hashing failed";
        resp->setBody("Internal Server Error");
        co_return;
    }
    std::string password_hash = hashed_password + "," + salt; // 存储哈希值，盐单独存储

    std::string get_role_query = "SELECT role FROM users WHERE username='" + username + "' AND password_hash='" + password_hash + "'";
    M_SYLAR_LOG_INFO(j_logger) << "execute sql: " << get_role_query;
    MySQLResp::ptr role = co_await m_sylar::DB::Mysql::getInstance()->executeQuery(get_role_query);
    M_SYLAR_LOG_INFO(j_logger) << "foramtting database response";
    role->formatDate();
        for(auto row = role->nextRow(); row; row = role->nextRow()) {
            for(auto value = row.nextValue(); value; value = row.nextValue()) {
                std::cout << value.get() << " ";
            }
            std::cout << std::endl;
        }
        std::cout << "------------------" << std::endl;
        role->resetRow();

    std::string role_val = (*role)["role"][0]; 
    M_SYLAR_LOG_INFO(j_logger) << "user " << username << " login with role " << role_val;

    resp->setBody(JWT::generateJWT(username, RolePermissions::RoleFromString(role_val)));
    co_return;
}


