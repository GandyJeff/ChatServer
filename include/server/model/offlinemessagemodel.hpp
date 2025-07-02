#ifndef OFFLINEMESSAGEMODEL_H
#define OFFLINEMESSAGEMODEL_H
#include <string>
#include <vector>

// 提供离线消息表的操作接口方法
class OfflineMsgModel
{
public:
    // 存取用户的离线消息
    void insert(int user_id, std::string msg);

    // 删除用户的离线消息
    void remove(int user_id);

    // 查询用户的离线消息
    std::vector<std::string> query(int user_id);
};

#endif