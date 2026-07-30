#pragma once
#include "publicHeader.hpp"
#include <nlohmann/json.hpp>
#include "connection.hpp"
#include "MessageList.hpp"


namespace chatter {
class WsMessage {
public:
    explicit WsMessage() = default;
    ~WsMessage() = default;

    enum class State {
        EMPTY,          // 无数据
        NORMAL,         // 正常
        BAD             // 解析错误
    };

    int load(const nlohmann::json& json);
    int load(const std::string& raw_json);

    WsMessage& setStatusCode(http::StatusCode status_code, const std::string& reason = "");
    WsMessage& setReason(const std::string& reason = "") {m_state = State::NORMAL; m_reason = reason; return *this; }
    WsMessage& setContent(const std::string& content) {m_state = State::NORMAL; m_content = content; return *this; }
    WsMessage& setFrom(const JettyCat::chat::userId id) {m_state = State::NORMAL; m_from = id; return *this; }
    WsMessage& setTo(const JettyCat::chat::userId id) {m_state = State::NORMAL; m_to = id; return *this; }

    [[nodiscard]] inline http::StatusCode getStatusCode() const {return m_code;}
    [[nodiscard]] inline std::string getReason() const {return m_reason;}
    [[nodiscard]] inline std::string getContent() const {return m_content;}
    [[nodiscard]] inline JettyCat::chat::userId getFrom() const {return m_from;}
    [[nodiscard]] inline JettyCat::chat::userId getTo() const {return m_to;}

    [[nodiscard]] std::string dump();
    [[nodiscard]] inline State getState() const {return m_state;}
    [[nodiscard]] inline bool empty() const {return m_state == State::EMPTY;}

private:
    State m_state {State::EMPTY};
    http::StatusCode m_code{http::StatusCode::ok};      // 复用http状态
    JettyCat::chat::userId m_from{0};                   // 消息来源, 0表示系统消息,其他代表用户id
    JettyCat::chat::userId m_to{0};                     // 消息接受者,0表示内部消息,不需要用户查看,其余的待定
    std::string m_reason{"ok"};                         // 复用http reson,通常会在设置好code后自动改变,除非手动改变
    std::string m_content;                              // 消息内容,通常填入一个json字段,来源于MessageList.hpp中Message结构体
};

}





class Chatter{
public:
    Chatter() = default;
    ~Chatter() = default;

    // interface
public:
    static void registeUrl(m_sylar::http::HttpServer::ptr server);

    static m_sylar::Task<void> co_connect(m_sylar::http::HttpSession::ptr session);

    static m_sylar::Task<void> coGetChatterList(m_sylar::http::HttpSession::ptr session);
};


