#include "files.hpp"
#include "login/tools.hpp"
#include "files/service/FileService.hpp"
#include "http/Response.hpp"

static m_sylar::Logger::ptr j_logger = M_SYLAR_LOG_NAME("jettyCat");

namespace JettyCat::file {

// URL 百分号解码（query string 中的 '/'(%2F) 等需还原后再做前缀比对）
static std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') out.push_back(' ');
        else out.push_back(s[i]);
    }
    return out;
}

// 把统一响应体写入一个 HttpSession（协议层渲染）
static void sendResp(const http::HttpSession::ptr& session, const chatter::resp::HttpResponse& response) {
    auto http_resp = session->getResponse();
    http_resp->appendHeader("Content-Type", "application/json");
    http_resp->setBody(response.dump());
    http_resp->setStatus(static_cast<http::StatusCode>(response.getCode()));
}

// 便捷重载：仅凭 code + msg 直接写响应（用于鉴权/协议层错误）
static void sendResp(const http::HttpSession::ptr& session, int code, const std::string& msg) {
    chatter::resp::HttpResponse response;
    response.setCode(code).setMsg(msg);
    sendResp(session, response);
}

// 文件业务服务：由真实 FileDao 装配。
// 依赖在这里一次性注入，co_ 只做协议层的事。
static service::FileService& getFileService() {
    static service::FileService instance{
        std::make_shared<dao::FileDao>()
    };
    return instance;
}

// ---------------------------------------------------------------------------
// HTTP 接口: POST /files/sts
// 返回临时凭证 + minio 端点/桶 信息 + 限定资源路径, 供前端直传
// ---------------------------------------------------------------------------
m_sylar::Task<void> files::coGetSts(m_sylar::http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    const std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(j_logger) << "coGetSts, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：取参数（path 为 query string 时 '/' 会被编码为 %2F，需先还原）
    const std::string duration_str = req->getParam("duration");
    const std::string resource_path = urlDecode(req->getParam("path"));
    const std::string access_str = req->getParam("access");

    // 业务逻辑交给 service（资源路径权限规则 + 读写权限 + 申请凭证）
    const auto result = co_await getFileService().getSts(
        JWT::parserPayload(jwt), duration_str, resource_path, access_str);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(j_logger) << "coGetSts, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

void files::registeUrl(m_sylar::http::HttpServer::ptr server) {
    if (server == nullptr) {
        M_SYLAR_LOG_ERROR(j_logger) << "invalid http server, http server is nullptr";
        return;
    }
    server->POST("/files/sts", files::coGetSts);
}

} // namespace JettyCat::file