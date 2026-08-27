#include "files.hpp"
#include "files/upload.hpp"
#include "files/fetch.hpp"

static m_sylar::Logger::ptr j_logger = M_SYLAR_LOG_NAME("jettyCat");

namespace JettyCat::file {

void files::registeUrl(m_sylar::http::HttpServer::ptr server) {
    if (server == nullptr) {
        M_SYLAR_LOG_ERROR(j_logger) << "invalid http server, http server is nullptr";
        return;
    }
    upload::registeUrl(server);
    fetch::registeUrl(server);
}

} // namespace JettyCat::file