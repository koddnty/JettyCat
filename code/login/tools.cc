#include "tools.hpp"
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>

static m_sylar::Logger::ptr j_logger = M_SYLAR_LOG_NAME("jettyCat");
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
    int begin_pos = 0;
    int end_pos = roleStr.find(",");
    Role result = Role::UNKNOWN;
    while(end_pos != std::string::npos) {
        std::string role_part = roleStr.substr(begin_pos, end_pos - begin_pos);
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
        end_pos = roleStr.find(",", begin_pos);
    }
    return result;
}



// 生成JWT
std::string JWT::generateJWT(const std::string& username, RolePermissions::Role role) {
    nlohmann::json header = {
        {"typ", "JWT"},
        {"alg", "HS256"}
    };
    std::string role_str = RolePermissions::RoleToString(role);
    nlohmann::json payload = {
        {"username", username},
        {"role", role_str},
        {"exp", std::time(nullptr) + 3600} // 设置过期时间为1小时
    };

    std::string header_encoded = Encode::base64JWTEncode(header.dump());
    std::string payload_encoded = Encode::base64JWTEncode(payload.dump());
    std::string signature_input = header_encoded + "." + payload_encoded;
    m_sylar::ConfigVar<std::string>::ptr jwtKey = 
        m_sylar::ConfigManager::LookUp<std::string>("Permission_system.JWTKey", "1397iausdh*^^^&46", JettyCat_CONFIG_ID, "JWT密钥");
    std::string key = jwtKey->getValue();

    unsigned char signature[64];  // EVP_MAX_MD_SIZE
    unsigned int signature_len = sizeof(signature);

    unsigned char* signatureptr = HMAC(EVP_sha256(), key.c_str(), key.size(), 
                                reinterpret_cast<const unsigned char*>(signature_input.data()), 
                                signature_input.size(), signature, &signature_len);
    if(signatureptr == nullptr) {
        M_SYLAR_LOG_ERROR(j_logger) << "HMAC calculation failed";
        return "";
    }

    std::string signature_encoded = Encode::base64JWTEncode(std::string(reinterpret_cast<char*>(signature), signature_len));
    return header_encoded + "." + payload_encoded + "." + signature_encoded;
}

bool JWT::verifyJWT(m_sylar::http::HttpSession::ptr session) {
    
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
    
    return std::string(reinterpret_cast<char*>(buffer.data()), out_len);
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

