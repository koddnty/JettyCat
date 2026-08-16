#include "tools.hpp"
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>

static m_sylar::Logger::ptr j_logger = M_SYLAR_LOG_NAME("jettyCat");

m_sylar::ConfigVar<std::string>::ptr jwtKey = 
    m_sylar::ConfigManager::LookUp<std::string>("permission_system.JWTKey", "1397iausdh*^^^&46", JettyCat_CONFIG_ID, "JWT密钥");

m_sylar::ConfigVar<uint64_t>::ptr jwtTimeOut =
    m_sylar::ConfigManager::LookUp<uint64_t>("permission_system.JWTTimeOut", 1728000, JettyCat_CONFIG_ID, "JWT密钥超时时常");

// tools
std::string RolePermissions::RoleToString(RolePermissions::Role role) {
    std::string result;
    if(role & Role::USER) {
        result += "USER";
    } 
    if(role & Role::ADMIN) {
        if(!result.empty()) {
            result += ",";
        }
        result += "ADMIN";
    } 

    return result.empty() ? "UNKNOWN" : result;
}

RolePermissions::Role RolePermissions::RoleFromString(std::string roleStr) {
    roleStr += ",";
    size_t begin_pos = 0;
    size_t end_pos = roleStr.find(',');
    Role result = Role::UNKNOWN;
    while(end_pos != std::string::npos) {
        std::string role_part = roleStr.substr(begin_pos, end_pos - begin_pos);
        std::ranges::transform(role_part, role_part.begin(), ::toupper); // 去除空格
        if(role_part == "USER") {
            result = static_cast<Role>(result | Role::USER);
        } else if(role_part == "ADMIN") {
            result = static_cast<Role>(result | Role::ADMIN);
        }
        else {
            M_SYLAR_LOG_WARN(j_logger) << "Unknown role part: " << role_part;
            return Role::UNKNOWN;
        }
        begin_pos = end_pos + 1;
        end_pos = roleStr.find(',', begin_pos);
    }
    return result;
}


// 生成JWT
std::string JWT::generateJWT(const std::string& username, const RolePermissions::Role role, JettyCat::chat::userId user_id) {
    const nlohmann::json header = {
        {"typ", "JWT"},
        {"alg", "HS256"}
    };
    std::string role_str = RolePermissions::RoleToString(role);
    const nlohmann::json payload = {
        {"username", username},
        {"userid", user_id},
        {"role", role_str},
        {"exp", std::time(nullptr)} // 设置过期时间为1小时
    };

    const std::string header_encoded = Encode::base64JWTEncode(header.dump());
    const std::string payload_encoded = Encode::base64JWTEncode(payload.dump());
    const std::string signature_input = header_encoded + "." + payload_encoded;

    const std::string key = jwtKey->getValue();

    unsigned char signature[64];  // EVP_MAX_MD_SIZE
    unsigned int signature_len = sizeof(signature);

    const unsigned char* signature_ptr = HMAC(EVP_sha256(), key.c_str(), key.size(),
                                reinterpret_cast<const unsigned char*>(signature_input.data()), 
                                signature_input.size(), signature, &signature_len);
    if(signature_ptr == nullptr) {
        M_SYLAR_LOG_ERROR(j_logger) << "HMAC calculation failed";
        return "";
    }

    std::string signature_encoded = Encode::base64JWTEncode(std::string(reinterpret_cast<char*>(signature), signature_len));
    return header_encoded + "." + payload_encoded + "." + signature_encoded;
}

JWT::State JWT::verifyJWT(const m_sylar::http::HttpSession::ptr& session) {
    if(!session) {
        M_SYLAR_LOG_ERROR(j_logger) << "Session is null";
        return State::FAILED;
    }

    const std::string jwt = session->getRequest()->getCookie("jwttoken");
    return verifyJWT(jwt);
}

JWT::State JWT::verifyJWT(const std::string& jwt) {
    if(jwt.empty()) {
        return State::FAILED;
    }
    std::stringstream ss(jwt);
    std::string header_encoded, payload_encoded, signature_encoded;
    if(!std::getline(ss, header_encoded, '.') ||
       !std::getline(ss, payload_encoded, '.') ||
       !std::getline(ss, signature_encoded)) {
        return State::FAILED;
    }

    // 重新计算签名并比较
    const std::string key = jwtKey->getValue();
    const std::string signature_input = header_encoded + "." + payload_encoded;
    unsigned char signature[64];  // EVP_MAX_MD_SIZE
    unsigned int signature_len = sizeof(signature);

    const unsigned char* signature_ptr = HMAC(EVP_sha256(), key.c_str(), key.size(),
                                reinterpret_cast<const unsigned char*>(signature_input.data()), 
                                signature_input.size(), signature, &signature_len);
    if(signature_ptr == nullptr) {
        M_SYLAR_LOG_ERROR(j_logger) << "HMAC calculation failed";
        return State::FAILED;
    }
    const std::string cal_signature= Encode::base64JWTEncode(std::string(reinterpret_cast<char*>(signature), signature_len));

    if(cal_signature != signature_encoded) {
        return State::FAILED;
    }

    // 超时验证
    if (time(nullptr) > parserPayload(jwt).exp + jwtTimeOut->getValue()) {
        M_SYLAR_LOG_DEBUG(j_logger) << "jwt timed out, value: " << parserPayload(jwt).exp << " now : " << time(nullptr);
        return State::EXPIRED;
    }
    return State::SUCCESS;
}



// 获得jwt头部
JWT::Header JWT::parserHeader(const std::string& jwt) {
    std::stringstream ss(jwt);
    std::string header_encoded;
    if(!std::getline(ss, header_encoded, '.')) {
        return {};
    }
    // M_SYLAR_LOG_INFO(j_logger) << "Parsing JWT header, encoded: " << header_encoded;
    std::string header_json_str = Encode::base64JWTDecode(header_encoded);
    nlohmann::json header_json;
    try {
        header_json = nlohmann::json::parse(header_json_str);
    }
    catch (const nlohmann::json::parse_error& e) {
        
        M_SYLAR_LOG_ERROR(j_logger) << "Failed to parse JWT header JSON: " << e.what()
                                     << "\n header_json_str: " << header_json_str;
        return JWT::Header();
    }
    JWT::Header header;
    header.typ = header_json.value("typ", "");
    header.alg = header_json.value("alg", "");
    return header;
}

// 获得jwt负载
JWT::Payload JWT::parserPayload(const std::string& jwt) {
    std::stringstream ss(jwt);
    std::string payload_encoded;
    if(!std::getline(ss, payload_encoded, '.') ||
       !std::getline(ss, payload_encoded, '.')){
        return {};
    }
    // M_SYLAR_LOG_INFO(j_logger) << "Parsing JWT payload, encoded: " << payload_encoded;
    std::string payload_json_str = Encode::base64JWTDecode(payload_encoded);
    nlohmann::json payload_json {};
    try {
        payload_json = nlohmann::json::parse(payload_json_str);

    }
    catch (const nlohmann::json::parse_error& e) {
        M_SYLAR_LOG_ERROR(j_logger) << "Failed to parse JWT header JSON: " << e.what()
                                             << "\n header_json_str: " << payload_json_str;
        return {};
    }

    // 配置解析
    JWT::Payload payload;
    try {
        payload.user_name = payload_json.value("username", "");
        payload.user_id = payload_json.value("userid", -1);
        const std::string role_str = payload_json.value("role", "UNKNOWN");
        payload.role = RolePermissions::RoleFromString(role_str);
        payload.exp = payload_json.value("exp", 0);
    }
    catch (const std::exception& e) {
        M_SYLAR_LOG_ERROR(j_logger) << "Failed to convert string to uint64_t: " << e.what();
        return {};
    }
    return payload;
}







// 工具函数
// Base64 URL 安全编码函数
std::string Encode::base64JWTEncode(const std::string &input) {
    // 1. 估算编码后长度并分配缓冲区
    size_t encoded_len = 4 * ((input.size() + 2) / 3);
    std::vector<unsigned char> buffer(encoded_len + 1);

    // 2. 使用 OpenSSL 进行标准 Base64 编码
    int out_len = EVP_EncodeBlock(buffer.data(), 
                                  reinterpret_cast<const unsigned char*>(input.data()), 
                                  input.size());
    std::string result(reinterpret_cast<char*>(buffer.data()), out_len);

    // 3. 转换为 URL 安全格式并移除填充 '='
    for (char &c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    result.erase(std::remove(result.begin(), result.end(), '='), result.end());

    return result;
}
std::string Encode::base64JWTDecode(const std::string& input) {
    // 1. 转换回标准 Base64 格式
    std::string standard = input;
    for (char &c : standard) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    size_t padding = (4 - (standard.size() % 4)) % 4;
    standard.append(padding, '=');
    
    // 2. 解码
    size_t decoded_len = (standard.size() * 3) / 4;
    std::vector<unsigned char> buffer(decoded_len + 1);
    
    int out_len = EVP_DecodeBlock(buffer.data(), 
                                  reinterpret_cast<const unsigned char*>(standard.data()), 
                                  standard.size());
    
    if (out_len == -1) {
        return "";
    }
    
    // 3. 移除填充字节 - EVP_DecodeBlock 不会自动移除填充
    // 计算实际的填充字符个数并从输出长度中减去
    size_t padding_count = 0;
    for (auto it = standard.rbegin(); it != standard.rend() && *it == '='; ++it) {
        padding_count++;
    }
    
    return std::string(reinterpret_cast<char*>(buffer.data()), out_len - padding_count);
}
std::string Encode::base64Encode(const std::string &input) {
        // 1. 估算编码后长度并分配缓冲区
    size_t encoded_len = 4 * ((input.size() + 2) / 3);
    std::vector<unsigned char> buffer(encoded_len + 1);

    // 2. 使用 OpenSSL 进行标准 Base64 编码
    int out_len = EVP_EncodeBlock(buffer.data(), 
                                  reinterpret_cast<const unsigned char*>(input.data()), 
                                  input.size());
    std::string result(reinterpret_cast<char*>(buffer.data()), out_len);
    return result;
}
std::string Encode::base64Decode(const std::string& input) {
    // 1. 补回填充
    std::string standard = input;
    size_t padding = (4 - (standard.size() % 4)) % 4;
    standard.append(padding, '=');
    
    // 2. 解码
    size_t decoded_len = (standard.size() * 3) / 4;
    std::vector<unsigned char> buffer(decoded_len + 1);
    
    int out_len = EVP_DecodeBlock(buffer.data(), 
                                  reinterpret_cast<const unsigned char*>(standard.data()), 
                                  standard.size());
    
    if (out_len == -1) {
        return "";
    }
    
    // 3. 移除填充字节 - EVP_DecodeBlock 不会自动移除填充
    // 计算实际的填充字符个数并从输出长度中减去
    size_t padding_count = 0;
    for (auto it = standard.rbegin(); it != standard.rend() && *it == '='; ++it) {
        padding_count++;
    }
    return std::string(reinterpret_cast<char*>(buffer.data()), out_len - padding_count);
}




// 加密
int Hash::hashPBKDF2(const std::string& pw, std::string& passwd, std::string& salt_out) {
    unsigned char salt[16], hash[32];
    if(0 == RAND_bytes(salt, 16)) {
        return -1; // 生成随机盐失败
    }
    
    if(0 == PKCS5_PBKDF2_HMAC(pw.c_str(), pw.size(), 
                    salt, 16, 100000, 
                    EVP_sha256(), 32, hash)) {
        return -1; // 密码哈希失败
    }
    
    passwd = std::string(reinterpret_cast<char*>(hash), 32);
    salt_out = std::string(reinterpret_cast<char*>(salt), 16);
    return 0;
}
int Hash::hashPBKDF2WithSalt(const std::string &pw, std::string &passwd, const std::string &salt_out) {
    // if(salt_out.size() != 16) {
    //     M_SYLAR_LOG_ERROR(j_logger) << "Invalid salt length: " << salt_out.size();
    //     return -1; // 无效的盐长度
    // }
    unsigned char salt[16], hash[32];
    salt_out.copy(reinterpret_cast<char*>(salt), 16);

    if(0 == PKCS5_PBKDF2_HMAC(pw.c_str(), pw.size(), 
                    salt, 16, 100000, 
                    EVP_sha256(), 32, hash)) {
        return -1; // 密码哈希失败
    }
    
    passwd = std::string(reinterpret_cast<char*>(hash), 32);
    return 0;
}

// 直接返回数据库存储密码
int Hash::generatePassword(const std::string& password, std::string& password_hash, std::string& salt) {
    if(hashPBKDF2(password, password_hash, salt) == -1) {
        M_SYLAR_LOG_ERROR(j_logger) << "Password hashing failed";
        return -1;
    }
    password_hash = Encode::base64Encode(password_hash) + "," + Encode::base64Encode(salt);
    return 0;
}

// 直接验证服务器存储密码
// 明文密码 - 数据库存储密码（hash,salt）
bool Hash::verifyPassword(const std::string& password, const std::string& password_hash) {
    // 解析数据库存储的密码，提取哈希值和盐
    const size_t comma_pos = password_hash.find(',');
    if(comma_pos == std::string::npos) {
        M_SYLAR_LOG_ERROR(j_logger) << "Invalid password hash format : " << password_hash;
        return false;
    }
    const std::string hash_part = Encode::base64Decode(password_hash.substr(0, comma_pos));
    const std::string salt_part = Encode::base64Decode(password_hash.substr(comma_pos + 1));
    // M_SYLAR_LOG_INFO(j_logger) << "hash_part: " << password_hash.substr(0, comma_pos) << ", salt_part: " << password_hash.substr(comma_pos + 1);
    // M_SYLAR_LOG_INFO(j_logger) << "hash_part: " << hash_part << ", salt_part: " << salt_part;

    // 盐长度剪切
    // if(salt_part.size() > 16) {
    //     salt_part = salt_part.substr(0, 16);
    // }

    // 计算输入密码的哈希值
    std::string computed_hash;
    if(hashPBKDF2WithSalt(password, computed_hash, salt_part) == -1) {
        M_SYLAR_LOG_ERROR(j_logger) << "Password hashing failed during verification";
        return false;
    }

    // 结果比较
    // M_SYLAR_LOG_INFO(j_logger) << "Computed hash: " << Encode::base64Encode(computed_hash) << ", Stored hash: " << Encode::base64Encode(hash_part);
    return computed_hash == hash_part;
}



bool TemplateHeader::CORSALL(m_sylar::http::HttpSession::ptr session) {
    const auto resp = session->getResponse();
    resp->appendHeader("Access-Control-Allow-Origin", "http://localhost:8806");
    resp->appendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    resp->appendHeader("Access-Control-Allow-Headers", "Content-Type");
    resp->appendHeader("Access-Control-Max-Age", "86400");

    if(session->getRequest()->getMethod() == m_sylar::http::toString(m_sylar::http::Method::OPTIONS)) {
        resp->setStatus(m_sylar::http::StatusCode::ok);
        resp->setBody("");
        return false;
    }
    return true;
}


