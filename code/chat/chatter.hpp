#pragma once
#include "publicHeader.hpp"
#include <nlohmann/json.hpp>
#include "connection.hpp"
#include "Message.hpp"


/**
 * 聊天模块 http路由配置与消息处理
 */


namespace chatter {
inline ConfigVar<uint64_t>::ptr single_fetch_count =
    ConfigManager::LookUp<size_t>("chatter.single_fetch_count", 15, JettyCat_CONFIG_ID, "单次获取历史消息数量");



// websocket通信序列化/反序列化类
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
    WsMessage& setType(const std::string& type) {m_state = State::NORMAL; m_type = type; return *this; }

    [[nodiscard]] inline http::StatusCode getStatusCode() const {return m_code;}
    [[nodiscard]] inline std::string getReason() const {return m_reason;}
    [[nodiscard]] inline std::string getContent() const {return m_content;}
    [[nodiscard]] inline std::string getType() const {return m_type;}
    [[nodiscard]] inline JettyCat::chat::userId getFrom() const {return m_from;}
    [[nodiscard]] inline JettyCat::chat::userId getTo() const {return m_to;}

    [[nodiscard]] std::string dump();
    [[nodiscard]] inline State getState() const {return m_state;}
    [[nodiscard]] inline bool empty() const {return m_state == State::EMPTY;}

private:
    State m_state {State::EMPTY};
    http::StatusCode m_code{http::StatusCode::ok};      // 复用http状态
    std::string m_type;                                 // 消息类别,用于路由分发("private_message","group_message","fetch_inbox"等)
    JettyCat::chat::userId m_from{0};                   // 消息来源, 0表示系统消息,其他代表用户id
    JettyCat::chat::userId m_to{0};                     // 消息接受者,0表示内部消息,不需要用户查看,其余的待定
    std::string m_reason{"ok"};                         // 复用http reson,通常会在设置好code后自动改变,除非手动改变
    std::string m_content;                              // 消息内容,通常填入一个json字段,来源于MessageList.hpp中Message结构体
};




void registeUrl(const http::HttpServer::ptr& server, const websocket::WsServer::ptr& ws_server);
Task<void> co_FetchUserMessage(http::HttpSession::ptr session);     // 返回部分消息.
Task<void> co_FetchGroupMessage(http::HttpSession::ptr session);     // 返回部分消息.
}






