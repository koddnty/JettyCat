#include "chatter.hpp"

namespace chatter {
WsMessage& WsMessage::setStatusCode(http::StatusCode status_code, const std::string& reason) {
    m_code = status_code;
    if (!reason.empty()) {
        m_reason = http::toString(status_code);
    }
    return *this;
}
}