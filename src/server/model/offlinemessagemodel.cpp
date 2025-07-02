#include "offlinemessagemodel.hpp"
#include "db.h"

// 存取用户的离线消息
void OfflineMsgModel::insert(int user_id, std::string msg)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into OfflineMessage values('%d','%s')", user_id, msg.c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 删除用户的离线消息
void OfflineMsgModel::remove(int user_id)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "delete from OfflineMessage where user_id = %d ", user_id);

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 查询用户的离线消息
std::vector<std::string> OfflineMsgModel::query(int user_id)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select message from OfflineMessage where user_id = %d", user_id);

    std::vector<std::string> vec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            // 把user_id用户的所有离线消息放入vec中返回
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}