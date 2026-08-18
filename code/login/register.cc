#include "register.hpp"
#include <cstdlib>
#include <ctime>

#include "chat/Message.hpp"

static m_sylar::Logger::ptr j_logger = M_SYLAR_LOG_NAME("jettyCat");
using namespace m_sylar;



// interface
void Register::registeUrl(m_sylar::http::HttpServer::ptr server) {
    srand((unsigned)time(NULL));
    if(server == nullptr)
    {
        M_SYLAR_LOG_ERROR(j_logger) << "invalid http server, http server is nullptr";
        return;
    }
    server->GET("/test", Register::test);
    server->GET("/registe/getRegCode", Register::coGetRegCode);
    server->POST("/login/jwt", Register::coLogin);
    server->POST("/registe/registe", Register::registe);
}


/**
    @brief 获取注册验证码
        生成redis验证码，key为reg_code_时间戳，value为8位随机数，过期时间为时间戳
        time_limit：验证码过期时间，单位为秒，前端管理员传入。
*/
// 获取注册验证码
m_sylar::Task<void> Register::coGetRegCode(m_sylar::http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if(!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 验证身份
    std::string jwt = req->getCookie("jwttoken");
    JWT::Header header;
    header = JWT::parserHeader(jwt);
    if(jwt.empty() || JWT::State::SUCCESS != JWT::verifyJWT(session)) {
        nlohmann::json j;
        j["status"] = "failed";
        if (JWT::verifyJWT(session) == JWT::State::EXPIRED) {
            j["error"] = "FORBIDDEN: token expired";
            resp->setStatus(http::StatusCode::unauthorized);
        }
        else {
            j["error"] = "FORBIDDEN: Invalid or missing JWT token";
            resp->setStatus(http::StatusCode::forbidden);
        }

        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        co_await session->co_sendResp();
        co_return;
    }

    JWT::Payload payload = JWT::parserPayload(jwt);
    if(payload.role != RolePermissions::ADMIN) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "FORBIDDEN: Insufficient permissions : " + RolePermissions::RoleToString(payload.role);
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::forbidden);
        co_await session->co_sendResp();
        co_return;
    }

    // 获取请求参数
    std::string time_limit = "time-limit";          // 验证码过期时间，单位为秒
    std::string valid_times = "valid-times";        // 验证码有效次数，过期时间未到但已使用次数超过valid_times也会失效
    std::string uri = req->getUri();
    time_limit = req->getParam(time_limit);
    valid_times = req->getParam(valid_times);
    // M_SYLAR_LOG_INFO(j_logger) << "generate one registe code" << std::endl;

    // 生成验证码并存入redis
    int num = random() % 90000000 + 10000000;
    std::string cmd = "set reg_code_" + std::to_string(num) + " " + valid_times + " EX " + time_limit;
    RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    // 响应
    if(reply->getState() == IOState::SUCCESS && reply->asString() == "OK"){
        resp->setBody(std::to_string(num));
    }
    else if(reply->getState() == IOState::TIMEOUT){
        resp->setBody("timeout");
    }
    co_await session->co_sendResp();
    co_return;
}

// 注册
m_sylar::Task<void> Register::registe(m_sylar::http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if(!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 获取请求参数 (body 中的 JSON)
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req->getBody());
    } catch (const std::exception& e) {
        body = nlohmann::json::object();
    }
    std::string username = body.value("username", "");
    std::string password = body.value("password", "");
    std::string reg_code = body.value("reg_code", "");

    // 参数验证
    if(username.empty() || password.empty() || reg_code.empty()) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Missing required parameters";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }
    if(username.size() > 32 || password.size() > 32) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Username or password too long";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }
    if(username.size() < 3 || password.size() < 6) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Username must be at least 3 characters and password must be at least 6 characters";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    // 验证验证码
    std::string cmd = "DECR reg_code_" + reg_code;
    RedisResp::ptr reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery(cmd);
    if(reply->getState() != IOState::SUCCESS || reply->asInt() <= -1){
        reply = co_await m_sylar::DB::Redis::getInstance()->executeQuery("DEL reg_code_" + reg_code); // 删除过期或无效的验证码
        if(reply->getState() != IOState::SUCCESS){
            M_SYLAR_LOG_ERROR(j_logger) << "Failed to delete invalid reg code: " << reg_code;
        }
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Invalid registration code";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::bad_request);
        co_await session->co_sendResp();
        co_return;
    }

    // 哈希密码并生成盐
    std::string hashed_password, salt;
    // if(Hash::hashPBKDF2(password, hashed_password, salt) == -1) {
    //     M_SYLAR_LOG_ERROR(j_logger) << "Password hashing failed";
    //     nlohmann::json j;
    //     j["status"] = "error";
    //     j["error"] = "internal server error";
    //     resp->setBody(j.dump());
    //     resp->setStatus(http::StatusCode::internal_server_error);
    //     co_return;
    // }
    int rt = Hash::generatePassword(password, hashed_password, salt);
    if(rt == -1) {
        M_SYLAR_LOG_ERROR(j_logger) << "Password hashing failed";
        nlohmann::json j;
        j["status"] = "error";
        j["error"] = "internal server error";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::internal_server_error);
        co_await session->co_sendResp();
        co_return;
    }
    // std::string password_hash = hashed_password; // 存储哈希值，盐单独存储

    // 插入
    const std::string insert_user_query = "INSERT INTO users (username, password_hash, role) VALUES (?, ?, 'USER')";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt stmt {conn_wrap};
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(insert_user_query, username, hashed_password);
    }

    if(state != IOState::SUCCESS) {
        nlohmann::json j;
        if(state == IOState::TIMEOUT) {
            j["status"] = "error";
            j["error"] = "Internal Server Error";
            resp->setStatus(http::StatusCode::internal_server_error);
        } else {
            j["status"] = "failed";
            j["error"] = "Failed to register user, possibly due to duplicate username";
            resp->setStatus(http::StatusCode::internal_server_error);
        }
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        co_await session->co_sendResp();
        co_return;
    }

    // 查找分配的id
    std::string find_user_id = "select users.user_id from users where username = '" + username + "';";
    auto resp_find_user_id = co_await DB::Mysql::getInstance()->executeQuery(find_user_id);
    resp_find_user_id->formatDate();
    if (resp_find_user_id->getState() != IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(j_logger) << "failed to fetch all users in MySQL query";
    }
    JettyCat::chat::userId user_id = std::stoi((*resp_find_user_id)["user_id"][0]);


    // 构建jwt并响应
    std::string jwt = JWT::generateJWT(username, RolePermissions::USER, user_id);
    nlohmann::json j;
    j["status"] = "success";

    std::string cookie = "jwttoken=" + jwt + "; HttpOnly; SameSite=Strict; Path=/";
    resp->appendHeader("Content-Type", "application/json");
    resp->appendHeader("Set-Cookie", cookie);
    resp->setBody(j.dump());
    resp->setStatus(http::StatusCode::ok);
    co_await session->co_sendResp();
    co_return;
} 

// 登陆
m_sylar::Task<void> Register::coLogin(m_sylar::http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    http::Response::ptr resp = session->getResponse();
    if(!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 获取请求参数 (body 中的 JSON)
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req->getBody());
    } catch (const std::exception& e) {
        body = nlohmann::json::object();
    }
    std::string username = body.value("username", "");
    std::string password = body.value("password", "");


    // std::string hashed_password, salt;
    // if(Hash::hashPBKDF2(password, hashed_password, salt) == -1) {
    //     M_SYLAR_LOG_ERROR(j_logger) << "Password hashing failed";
    //     resp->setBody("Internal Server Error");
    //     co_return;
    // }
    // // std::string password_hash = hashed_password + "," + salt; // 存储哈希值，盐单独存储
    // std::string password_hash = Encode::base64Encode(hashed_password) + "," + Encode::base64Encode(salt);

    // 密码、角色查询
    IOState state = IOState::SUCCESS;
    auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt<STMT_Text<16>, STMT_Text<255>, int> stmt{conn_wrap};
    std::string get_role_query = "SELECT role, password_hash, user_id FROM users WHERE username= ? ;";
    state = co_await stmt.co_execute(get_role_query, username);
    state = co_await stmt.co_storeAll();
    state = co_await stmt.co_fetchAll();
    auto result = stmt.getResult().getAll();
    if (state != IOState::SUCCESS) {
        M_SYLAR_LOG_ERROR(gdb_logger) << "failed to run stmt cmd\n";
    }
    if (result.empty()) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Invalid username or password";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::ok);
        co_await session->co_sendResp();
        co_return;
    }
    const std::string role_val = std::get<0>(result[0]).toString();
    const std::string stored_password_hash = std::get<1>(result[0]).toString();
    const JettyCat::chat::userId user_id = std::get<2>(result[0]);


    // 验证密码
    if(false == Hash::verifyPassword(password, stored_password_hash)) {
        nlohmann::json j;
        j["status"] = "failed";
        j["error"] = "Invalid username or password";
        resp->appendHeader("Content-Type", "application/json");
        resp->setBody(j.dump());
        resp->setStatus(http::StatusCode::unauthorized);
        co_await session->co_sendResp();
        co_return;
    }
    // M_SYLAR_LOG_INFO(j_logger) << "user " << username << " login with role " << role_val;
    nlohmann::json j;
    j["status"] = "success";
    std::string jwt = JWT::generateJWT(username, RolePermissions::RoleFromString(role_val), user_id);
    std::string cookie = "jwttoken=" + jwt + "; HttpOnly; SameSite=Strict; Path=/";
    resp->appendHeader("Content-Type", "application/json");
    resp->appendHeader("Set-Cookie", cookie);
    resp->setBody(j.dump());
    co_await session->co_sendResp();
    co_return;
}
