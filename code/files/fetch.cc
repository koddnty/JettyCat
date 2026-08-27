#include "fetch.hpp"
#include "login/tools.hpp"
#include "files/service/FileService.hpp"
#include "http/Response.hpp"

static m_sylar::Logger::ptr j_logger = M_SYLAR_LOG_NAME("jettyCat");

namespace JettyCat::file::fetch {

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

// 文件业务服务：依赖在这里一次性注入，co_ 只做协议层的事。
static service::FileService& getFileService() {
    static service::FileService instance{
        std::make_shared<dao::FileDao>(),
        std::make_shared<chatter::dao::FriendDao>(std::make_shared<chatter::dao::ProductDbProvider>()),
        std::make_shared<chatter::dao::GroupDao>(std::make_shared<chatter::dao::ProductDbProvider>()),
    };
    return instance;
}

// ---------------------------------------------------------------------------
// HTTP 接口: POST /files/fetch/sts
// 把当前用户所有可读位置的读权限写入策略，返回读凭证
// ---------------------------------------------------------------------------
m_sylar::Task<void> coFetchSts(m_sylar::http::HttpSession::ptr session) {
    http::Request::ptr req = session->getRequest();
    if (!TemplateHeader::CORSALL(session)) {
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：身份验证
    const std::string jwt = req->getCookie("jwttoken");
    if (jwt.empty() || JWT::verifyJWT(jwt) != JWT::State::SUCCESS) {
        M_SYLAR_LOG_WARN(j_logger) << "coFetchSts, unauthorized: invalid or missing JWT token";
        sendResp(session, 403, "FORBIDDEN: Invalid or missing JWT token");
        co_await session->co_sendResp();
        co_return;
    }

    // 协议层职责：取参数
    const std::string duration_str = req->getParam("duration");

    // 业务逻辑交给 service（收集可读位置 + 多前缀读凭证）
    const auto result = co_await getFileService().getFetchSts(
        JWT::parserPayload(jwt), duration_str);

    if (!result.isOk()) {
        M_SYLAR_LOG_WARN(j_logger) << "coFetchSts, business failed: " << result.getMsg();
    }
    sendResp(session, result);
    co_await session->co_sendResp();
    co_return;
}

void registeUrl(m_sylar::http::HttpServer::ptr server) {
    if (server == nullptr) {
        M_SYLAR_LOG_ERROR(j_logger) << "invalid http server, http server is nullptr";
        return;
    }
    server->POST("/files/fetch/sts", coFetchSts);
}

} // namespace JettyCat::file::fetch