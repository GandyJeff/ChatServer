#ifndef DB_H
#define DB_H

#include <mysql/mysql.h>
#include <string>

class MySQL
{
public:
    // 初始化数据库连接
    MySQL();

    // 释放数据库连接资源
    ~MySQL();

    // 连接数据库
    bool connect();

    // 更新操作
    bool update(std::string sql);

    // 查询操作
    MYSQL_RES *query(std::string sql);

    // 获取连接
    MYSQL *getConnection();

private:
    // 成员变量
    MYSQL *_conn;
};

#endif