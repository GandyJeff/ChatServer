#include "friendmodel.hpp"
#include "db.h"

// 添加好友关系
void FriendModel::insert(int user_id, int friend_id)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into Friend values('%d','%d')", user_id, friend_id);

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 返回用户好友列表
vector<User> FriendModel::query(int user_id)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select a.id,a.name,a.state from User a inner join Friend b on b.friend_id = a.id where b.user_id = %d", user_id);

    vector<User> vec;
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
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                vec.push_back(user);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}