//
// Created by koddnty on 2026/7/14.
//
#include "MessageList.hpp"



namespace JettyCat::chat {
static m_sylar::Logger::ptr g_logger = M_SYLAR_LOG_NAME("jettyCat");


Message::Type Message::StringToType(const std::string& content) {
    std::string s_type = content;
    std::ranges::transform(content, s_type.begin(), ::toupper);
    const auto type = m_STT.find(s_type);
    if (type == m_STT.end()) {
        M_SYLAR_LOG_ERROR(g_logger) << "unknown message type: " << s_type;
        return Type::UNKNOWN;
    }
    return type->second;
}


std::string Message::TypeToString(const Type type) {
    const auto content = m_TTS.find(type);
    if (content == m_TTS.end()) {
        M_SYLAR_LOG_ERROR(g_logger) << "unknown message type : " << (int) type;
        return "BAD_TYPE";
    }
    return content->second;
}

std::string Message::dump() const {
    nlohmann::json j{};
    j["date"] = m_date;
    j["from"] = m_from;
    j["type"] = TypeToString(m_type);
    j["content"] = m_content;
    return j.dump();
}

int Message::load(const nlohmann::json& j) {
    try {
        if (j.contains("date") && j["date"].is_number()) {
            m_date = j["date"].get<uint64_t>();
        }
        if (j.contains("from") && j["from"].is_number_integer()) {
            m_from = j["from"].get<userId>();
        }
        if (j.contains("type") && j["type"].is_string()) {
            m_type = StringToType(j["type"].get<std::string>());
        }
        if (j.contains("content") && j["content"].is_string()) {
            m_content = j["content"].get<std::string>();
        }
        return 0;
    } catch (const std::exception& e) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to parse message: " << e.what();
        return -1;
    }
}

int Message::load(const std::string& raw_json) {
    try {
        nlohmann::json j = nlohmann::json::parse(raw_json);
        return load(j);
    } catch (const std::exception& e) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to parse message json: " << e.what();
        return -1;
    }
}


m_sylar::Task<State> sendToUser(const userId& user_id, const MessageList& message_list) {
    const std::string sql = "insert into user_message (sender_id, receiver_id, content, extra) values (?, ?, ?, ?)";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt stmt {conn_wrap};
    for (auto& msg_it : message_list) {
        IOState state = IOState::TIMEOUT;
        int count = 3;
        while (state == IOState::TIMEOUT && count--) {
            state = co_await stmt.co_execute(sql, msg_it.getFrom(), user_id, msg_it.getContent(), msg_it.dump());
        }

        // 状态检查
        switch (state) {
            case IOState::SUCCESS:
                continue;
            case IOState::TIMEOUT:
                M_SYLAR_LOG_ERROR(g_logger) << "execute sql time out: " << sql;
                co_return State::TIMEOUT;
            default:
                M_SYLAR_LOG_ERROR(g_logger) << "failed to execute sql: " << sql;
                co_return State::FAILED;
        }
    }

    co_return State::SUCCESS;
}


m_sylar::Task<State> sendToGroup(const groupId& group_id, const MessageList& message_list) {
    const std::string sql = "insert into group_message (user_id, group_id, content, extra) values (?, ?, ?, ?)";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt stmt {conn_wrap};
    for (auto& msg_it : message_list) {
        IOState state = IOState::TIMEOUT;
        int count = 3;
        while (state == IOState::TIMEOUT && count--) {
            state = co_await stmt.co_execute(sql, msg_it.getFrom(), group_id, msg_it.getContent(), msg_it.dump());
        }

        // 状态检查
        switch (state) {
        case IOState::SUCCESS:
            continue;
        case IOState::TIMEOUT:
            M_SYLAR_LOG_ERROR(g_logger) << "execute sql time out: " << sql;
            co_return State::TIMEOUT;
        default:
            M_SYLAR_LOG_ERROR(g_logger) << "failed to execute sql: " << sql;
            co_return State::FAILED;
        }
    }

    co_return State::SUCCESS;
}


m_sylar::Task<State> fetchFromGroup(const groupId& group_id, size_t offset, MessageList& message_list) {
    const std::string sql = "select user_id, msg_type, content, UNIX_TIMESTAMP(send_time) from group_message where group_message.group_id = ? order by group_message.send_time desc limit 10 offset ?";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt<int, STMT_Text<36>, STMT_Text<2048>, uint64_t> stmt {conn_wrap};

    // 执行语句
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, group_id, offset);
    }
    // 状态检查
    switch (state) {
    case IOState::SUCCESS:
        break;
    case IOState::TIMEOUT:
        M_SYLAR_LOG_ERROR(g_logger) << "execute sql time out: " << sql;
        co_return State::TIMEOUT;
    default:
        M_SYLAR_LOG_ERROR(g_logger) << "failed to execute sql: " << sql;
        co_return State::FAILED;
    }

    // 结果获取
    if(IOState::SUCCESS != co_await stmt.co_storeAll()) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to store all result: " << sql;
        co_return State::FAILED;
    }
    if (IOState::SUCCESS != co_await stmt.co_fetchAll()) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to fetch all results: " << sql;
        co_return State::FAILED;
    }

    // 结果写入
    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        Message message;
        message.setDate(std::get<3>(it))
            .setContent(std::get<2>(it).toString())
            .setType(std::get<1>(it).toString())
            .setFrom(std::get<0>(it));
        message_list.push_back(message);
    }
    co_return State::SUCCESS;
}


m_sylar::Task<State> fetchFromInbox(const userId& sender_id, size_t offset, MessageList& message_list) {
    const std::string sql = "select msg_type, content, UNIX_TIMESTAMP(send_time) from user_message where user_message.sender_id = ? order by user_message.send_time desc limit 10 offset ?";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt<STMT_Text<36>, STMT_Text<2048>, uint64_t> stmt {conn_wrap};

    // 执行语句
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, sender_id, offset);
    }
    // 状态检查
    switch (state) {
    case IOState::SUCCESS:
        break;
    case IOState::TIMEOUT:
        M_SYLAR_LOG_ERROR(g_logger) << "execute sql time out: " << sql;
        co_return State::TIMEOUT;
    default:
        M_SYLAR_LOG_ERROR(g_logger) << "failed to execute sql: " << sql;
        co_return State::FAILED;
    }

    // 结果获取
    if(IOState::SUCCESS != co_await stmt.co_storeAll()) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to store all result: " << sql;
        co_return State::FAILED;
    }
    if (IOState::SUCCESS != co_await stmt.co_fetchAll()) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to fetch all results: " << sql;
        co_return State::FAILED;
    }

    // 结果写入
    for (auto result = stmt.getResult().getAll(); auto& it : result) {
        auto cs = std::get<1>(it).toString();
        Message message;
        message.setDate(std::get<2>(it))
            .setContent(std::get<1>(it).toString())
            .setType(std::get<0>(it).toString())
            .setFrom(sender_id);
        message_list.push_back(message);
    }
    co_return State::SUCCESS;
}
};