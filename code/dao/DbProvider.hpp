#pragma once
#include "publicHeader.hpp"

namespace chatter::dao {

/**
 * @brief 数据库连接
 */
class DbProvider {
public:
    virtual ~DbProvider() = default;

    virtual std::shared_ptr<ConnectWrapper<MySQLConn, MySQLResp>> borrowConn() = 0;
};




class ProductDbProvider final : public DbProvider {
public:
    std::shared_ptr<ConnectWrapper<MySQLConn, MySQLResp>> borrowConn() override {
        return DB::Mysql::getInstance()->borrowOneConn();
    }
};

}
