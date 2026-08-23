//
// Created by koddnty on 2026/7/14.
//
#include "Message.hpp"



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

std::string Message::TypeToDbString(const Type type) {
    switch (type) {
        case Type::TEXT:  return "text";
        case Type::IMAGE: return "image";
        case Type::VOICE: return "voice";
        case Type::VIDEO: return "video";
        case Type::FILE:  return "file";
        default:          return "text";
    }
}

std::string Message::dump() const {
    nlohmann::json j{};
    j["date"] = m_date;
    j["from"] = std::to_string(m_from); // user_id 为 snowflake 大整数, 以字符串传输避免精度丢失
    j["type"] = TypeToString(m_type);
    j["content"] = m_content;
    return j.dump();
}

int Message::load(const nlohmann::json& j) {
    try {
        if (j.contains("date") && j["date"].is_number()) {
            m_date = j["date"].get<uint64_t>();
        }
        if (j.contains("from")) {
            if (j["from"].is_number_integer()) {
                m_from = j["from"].get<int64_t>();
            } else if (j["from"].is_string()) {
                m_from = std::stoll(j["from"].get<std::string>());
            }
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


m_sylar::Task<DBState> sendToUser(const userId& user_id, const MessageList& message_list) {
    const std::string sql = "insert into user_message (sender_snow_id, receiver_snow_id, msg_type, content, extra) values (?, ?, ?, ?, ?)";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt stmt {conn_wrap};
    for (auto& msg_it : message_list) {
        IOState state = IOState::TIMEOUT;
        int count = 3;
        while (state == IOState::TIMEOUT && count--) {
            state = co_await stmt.co_execute(sql, msg_it.getFrom(), user_id, msg_it.TypeToDbString(msg_it.getType()), msg_it.getContent(), msg_it.dump());
        }

        // 状态检查
        switch (state) {
            case IOState::SUCCESS:
                continue;
            case IOState::TIMEOUT:
                M_SYLAR_LOG_ERROR(g_logger) << "execute sql time out: " << sql;
                co_return DBState::TIMEOUT;
            default:
                M_SYLAR_LOG_ERROR(g_logger) << "failed to execute sql: " << sql;
                co_return DBState::FAILED;
        }
    }

    co_return DBState::SUCCESS;
}


m_sylar::Task<DBState> sendToGroup(const groupId& group_id, const MessageList& message_list) {
    const std::string sql = "insert into group_message (user_snow_id, group_snow_id, msg_type, content, extra) values (?, ?, ?, ?, ?)";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt stmt {conn_wrap};
    for (auto& msg_it : message_list) {
        IOState state = IOState::TIMEOUT;
        int count = 3;
        while (state == IOState::TIMEOUT && count--) {
            state = co_await stmt.co_execute(sql, msg_it.getFrom(), group_id, msg_it.TypeToDbString(msg_it.getType()), msg_it.getContent(), msg_it.dump());
        }

        // 状态检查
        switch (state) {
        case IOState::SUCCESS:
            continue;
        case IOState::TIMEOUT:
            M_SYLAR_LOG_ERROR(g_logger) << "execute sql time out: " << sql;
            co_return DBState::TIMEOUT;
        default:
            M_SYLAR_LOG_ERROR(g_logger) << "failed to execute sql: " << sql;
            co_return DBState::FAILED;
        }
    }

    co_return DBState::SUCCESS;
}


m_sylar::Task<DBState> fetchFromGroup(const groupId& group_id, size_t offset, MessageList& message_list) {
    const std::string sql = "select user_snow_id, msg_type, content, UNIX_TIMESTAMP(send_time) from group_message where group_message.group_snow_id = ? order by group_message.send_time desc limit 10 offset ?";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt<int64_t, STMT_Text<36>, STMT_Text<2048>, uint64_t> stmt {conn_wrap};

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
        co_return DBState::TIMEOUT;
    default:
        M_SYLAR_LOG_ERROR(g_logger) << "failed to execute sql: " << sql;
        co_return DBState::FAILED;
    }

    // 结果获取
    if(IOState::SUCCESS != co_await stmt.co_storeAll()) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to store all result: " << sql;
        co_return DBState::FAILED;
    }
    if (IOState::SUCCESS != co_await stmt.co_fetchAll()) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to fetch all results: " << sql;
        co_return DBState::FAILED;
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
    co_return DBState::SUCCESS;
}

m_sylar::Task<DBState> fetchFromInbox(const userId& sender_id, const userId& receiver_id, size_t offset, MessageList& message_list) {
    const std::string sql = "select sender_snow_id, msg_type, content, UNIX_TIMESTAMP(send_time) from user_message where user_message.sender_snow_id = ? and user_message.receiver_snow_id = ? order by user_message.send_time desc limit 10 offset ?";
    const auto conn_wrap = DB::Mysql::getInstance()->borrowOneConn();
    MySQLStmt<int64_t, STMT_Text<36>, STMT_Text<2048>, uint64_t> stmt {conn_wrap};

    // 执行语句
    IOState state = IOState::TIMEOUT;
    int count = 3;
    while (state == IOState::TIMEOUT && count--) {
        state = co_await stmt.co_execute(sql, sender_id, receiver_id, offset);
    }
    // 状态检查
    switch (state) {
    case IOState::SUCCESS:
        break;
    case IOState::TIMEOUT:
        M_SYLAR_LOG_ERROR(g_logger) << "execute sql time out: " << sql;
        co_return DBState::TIMEOUT;
    default:
        M_SYLAR_LOG_ERROR(g_logger) << "failed to execute sql: " << sql;
        co_return DBState::FAILED;
    }

    // 结果获取
    if(IOState::SUCCESS != co_await stmt.co_storeAll()) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to store all result: " << sql;
        co_return DBState::FAILED;
    }
    if (IOState::SUCCESS != co_await stmt.co_fetchAll()) {
        M_SYLAR_LOG_ERROR(g_logger) << "failed to fetch all results: " << sql;
        co_return DBState::FAILED;
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
    co_return DBState::SUCCESS;
}
};