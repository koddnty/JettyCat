#include "FileDao.hpp"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <sstream>
#include <cstring>
#include <cctype>
#include <ctime>
#include <iomanip>

static m_sylar::Logger::ptr j_logger = M_SYLAR_LOG_NAME("jettyCat");

namespace JettyCat::file::dao {

// MinIO 配置 (conf/jettyCat.json -> "minio")
static m_sylar::ConfigVar<std::string>::ptr g_minio_endpoint =
    m_sylar::ConfigManager::LookUp<std::string>("minio.endpoint", "127.0.0.1:9000", JettyCat_CONFIG_ID, "minio endpoint");
static m_sylar::ConfigVar<std::string>::ptr g_minio_access_key =
    m_sylar::ConfigManager::LookUp<std::string>("minio.accessKey", "", JettyCat_CONFIG_ID, "minio access key");
static m_sylar::ConfigVar<std::string>::ptr g_minio_secret_key =
    m_sylar::ConfigManager::LookUp<std::string>("minio.secretKey", "", JettyCat_CONFIG_ID, "minio secret key");
static m_sylar::ConfigVar<std::string>::ptr g_minio_region =
    m_sylar::ConfigManager::LookUp<std::string>("minio.region", "us-east-1", JettyCat_CONFIG_ID, "minio region");
static m_sylar::ConfigVar<std::string>::ptr g_minio_bucket =
    m_sylar::ConfigManager::LookUp<std::string>("minio.bucket", "jettycat", JettyCat_CONFIG_ID, "minio bucket");

// 十六进制 / SHA256 / HMAC-SHA256 / URL编码
namespace {

std::string toHex(const unsigned char* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0x0f]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

std::string sha256Hex(const std::string& input) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    return toHex(digest, SHA256_DIGEST_LENGTH);
}

std::string hmacSHA256(const std::string& key, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest, &len);
    return std::string(reinterpret_cast<char*>(digest), len);
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0f]);
            out.push_back(hex[c & 0x0f]);
        }
    }
    return out;
}

// libcurl 响应写入回调
size_t writeCb(void* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

// 从 XML 中提取某个标签的内容
std::string xmlExtract(const std::string& xml, const std::string& tag) {
    std::string open = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    size_t b = xml.find(open);
    if (b == std::string::npos) return "";
    b += open.size();
    size_t e = xml.find(close, b);
    if (e == std::string::npos) return "";
    return xml.substr(b, e - b);
}

} // namespace








// SigV4 签名
namespace {

// 生成 AWS SigV4 签名的 Authorization 头
// @return 完整 Authorization header 值
std::string signV4(const std::string& method,
                   const std::string& host,
                   const std::string& canonical_uri,
                   const std::string& canonical_query,
                   const std::string& headers_str,   // 形如 "content-type:...\nhost:...\nx-amz-date:...\n"
                   const std::string& signed_headers, // "content-type;host;x-amz-date"
                   const std::string& payload_hash,
                   const std::string& amz_date,
                   const std::string& date_stamp,
                   const std::string& region,
                   const std::string& service,
                   const std::string& access_key,
                   const std::string& secret_key) {

    std::string canonical_request = method + "\n" +
                                    canonical_uri + "\n" +
                                    canonical_query + "\n" +
                                    headers_str + "\n" +
                                    signed_headers + "\n" +
                                    payload_hash;

    std::string scope = date_stamp + "/" + region + "/" + service + "/aws4_request";
    std::string string_to_sign = "AWS4-HMAC-SHA256\n" +
                                 amz_date + "\n" +
                                 scope + "\n" +
                                 sha256Hex(canonical_request);

    std::string k_date    = hmacSHA256("AWS4" + secret_key, date_stamp);
    std::string k_region  = hmacSHA256(k_date, region);
    std::string k_service = hmacSHA256(k_region, service);
    std::string k_signing = hmacSHA256(k_service, "aws4_request");
    std::string signature_bytes = hmacSHA256(k_signing, string_to_sign);
    std::string signature = toHex(reinterpret_cast<const unsigned char*>(signature_bytes.data()),
                                  signature_bytes.size());

    return "AWS4-HMAC-SHA256 Credential=" + access_key + "/" + scope +
           ", SignedHeaders=" + signed_headers +
           ", Signature=" + signature;
}

} // namespace








// FileDao 实现
std::string FileDao::getEndpoint() const { return g_minio_endpoint->getValue(); }
std::string FileDao::getBucket() const   { return g_minio_bucket->getValue(); }
std::string FileDao::getRegion() const   { return g_minio_region->getValue(); }

policy::Policy FileDao::buildResourcePolicy(const std::string& bucket, const std::string& resourcePath,
                                            const std::vector<policy::Action>& actions) {
    // 精确到资源路径前缀: arn:aws:s3:::bucket/<prefix> 及 <prefix>/*
    std::vector<std::string> resources;
    resources.push_back("arn:aws:s3:::" + bucket + "/" + resourcePath);
    resources.push_back("arn:aws:s3:::" + bucket + "/" + resourcePath + "*");

    policy::Policy p = policy::Policy::allow(actions, resources);
    p.setId("jettycat-object-policy");
    return p;
}

m_sylar::Task<StsCredential> FileDao::assumeRole(unsigned int durationSeconds,
                                                 const std::string& resourcePath,
                                                 const std::vector<policy::Action>& actions) const {
    StsCredential cred;
    const std::string bucket = g_minio_bucket->getValue();

    policy::Policy policyDoc;
    if (!resourcePath.empty()) {
        std::vector<policy::Action> acts = actions;
        if (acts.empty()) {
            acts = {policy::Action::GetObject};
        }
        policyDoc = buildResourcePolicy(bucket, resourcePath, acts);
    }
    co_return co_await assumeRoleWithPolicy(durationSeconds, policyDoc);
}




m_sylar::Task<StsCredential> FileDao::assumeRoleWithPolicy(unsigned int durationSeconds,
                                                           const policy::Policy& policyDoc) const {
    StsCredential cred;
    const std::string endpoint    = g_minio_endpoint->getValue();
    const std::string access_key  = g_minio_access_key->getValue();
    const std::string secret_key  = g_minio_secret_key->getValue();
    const std::string region      = g_minio_region->getValue();

    if (access_key.empty() || secret_key.empty()) {
        M_SYLAR_LOG_ERROR(j_logger) << "minio accessKey/secretKey not configured";
        co_return cred;
    }

    // UTC 时间
    time_t now = time(nullptr);
    tm tmv{};
    gmtime_r(&now, &tmv);
    char amz_date[32], date_stamp[16];
    strftime(amz_date, sizeof(amz_date), "%Y%m%dT%H%M%SZ", &tmv);
    strftime(date_stamp, sizeof(date_stamp), "%Y%m%d", &tmv);

    // STS AssumeRole 表单
    std::string form = "Action=AssumeRole&Version=2011-06-15&DurationSeconds=" + std::to_string(durationSeconds);
    // 精确权限控制: 附加内联策略, 把临时凭证限制在指定的资源路径前缀及动作下
    if (!policyDoc.empty()) {
        std::string policy_text = policyDoc.toJsonString();
        form += "&Policy=" + urlEncode(policy_text);
    }
    std::string payload_hash = sha256Hex(form);

    std::string host = endpoint;
    std::string uri = "/minio/sts";
    std::string query = "";

    // CanonicalHeaders (需按字典序): content-type, host, x-amz-date
    std::string canonical_headers =
        std::string("content-type:application/x-www-form-urlencoded\n") +
        "host:" + host + "\n" +
        "x-amz-date:" + amz_date + "\n";
    std::string signed_headers = "content-type;host;x-amz-date";

    std::string service = "sts";
    std::string auth = signV4("POST", host, uri, query, canonical_headers, signed_headers,
                              payload_hash, amz_date, date_stamp, region, service,
                              access_key, secret_key);

    // 通过 libcurl 发送
    std::string url = "http://" + endpoint + uri;
    std::string resp_body;

    CURL* curl = curl_easy_init();
    if (!curl) {
        M_SYLAR_LOG_ERROR(j_logger) << "curl_easy_init failed";
        co_return cred;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, ("Host: " + host).c_str());
    headers = curl_slist_append(headers, std::string("X-Amz-Date: " + std::string(amz_date)).c_str());
    headers = curl_slist_append(headers, ("Authorization: " + auth).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        M_SYLAR_LOG_ERROR(j_logger) << "sts request failed: " << curl_easy_strerror(rc);
        co_return cred;
    }
    if (http_code != 200) {
        M_SYLAR_LOG_ERROR(j_logger) << "sts request http error: " << http_code << ", body: " << resp_body;
        co_return cred;
    }

    // 解析 XML 响应
    cred.accessKeyId     = xmlExtract(resp_body, "AccessKeyId");
    cred.secretAccessKey = xmlExtract(resp_body, "SecretAccessKey");
    cred.sessionToken    = xmlExtract(resp_body, "SessionToken");
    cred.expiration      = xmlExtract(resp_body, "Expiration");

    if (cred.accessKeyId.empty() || cred.secretAccessKey.empty() || cred.sessionToken.empty()) {
        M_SYLAR_LOG_ERROR(j_logger) << "failed to parse sts response: " << resp_body;
        co_return StsCredential{};
    }
    M_SYLAR_LOG_INFO(j_logger) << "obtained sts credential, expire at " << cred.expiration;
    co_return cred;
}

} // namespace JettyCat::file::dao