#ifndef GROUPMODEL_H
#define GROUPMODEL_H

#include "group.hpp"
#include <vector>
#include <string>

#define CREATOR "creator"
#define NORMAL "normal"

using namespace std;

// 维护群组信息的操作接口方法
class GroupModel
{
public:
    // 创建群组
    bool creatGroup(Group &group);

    // 加入群组
    void addGroup(int group_id, int user_id, string role);

    // 查询用户所在群组信息
    vector<Group> queryGroups(int user_id);

    // 根据指定的group_id查询群组用户id列表，除user_id自己，主要用户群聊业务给群组其它成员群发消息
    vector<int> queryGroupUsers(int user_id, int group_id);
};

#endif