#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <string>
#include <cstring>
#include <vector>
#include <iostream>

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    // 用户基本业务管理相关事件处理回调注册
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREAT_GROUP_MSG, std::bind(&ChatService::creatGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addToGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));

        // // 在 ChatService 初始化 Redis 回调时，替换为纯打印逻辑
        // _redis.init_notify_handler([this](int user_id, string msg)
        //                            {
        // // 仅打印消息，不做任何业务处理（如解析 JSON、操作容器等）
        // std::cout << "【纯打印】收到消息：user_id=" << user_id << ", msg=" << msg << std::endl;
        // std::cout << "【纯打印】消息长度：" << msg.size() << "字节，首字符：" << (msg.empty() ? ' ' : msg[0]) << std::endl; });
    }
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器，空操作
        return [=](auto a, auto b, auto c)
        { LOG_ERROR << "msgid:" << msgid << " can not find handler!"; };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

// 处理登录业务
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = _userModel.query(id);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 该用户已登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;                                // 业务失败
            response["errmsg"] = "该账号已经登录,请输入其他账号"; // 业务失败

            // conn->send(response.dump());

            sendWithLengthPrefix(conn, response);
        }
        else
        {
            // 登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id);

            // 登录成功，更新用户状态信息 state offline => online
            user.setState("online");
            _userModel.updateState(user);

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0; // 业务成功
            response["id"] = user.getId();
            response["name"] = user.getName();

            // 查询该用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;

                // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
                _offlineMsgModel.remove(id);
            }

            // 查询该用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty())
            {
                vector<string> reVec;
                for (User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    reVec.push_back(js.dump());
                }
                response["friends"] = reVec;
            }

            // 查询用户的群组信息
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            if (!groupuserVec.empty())
            {
                // group:[{group_id:[xxx,xxx,xxx,xxx]}]
                vector<string> groupVec;
                for (Group &group : groupuserVec)
                {
                    json group_js;
                    group_js["id"] = group.getId();
                    group_js["groupname"] = group.getName();
                    group_js["groupdesc"] = group.getDesc();

                    vector<string> userVec;
                    for (GroupUser &user : group.getGroupUser())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userVec.push_back(js.dump());
                    }
                    group_js["users"] = userVec;
                    groupVec.push_back(group_js.dump());
                }
                response["groups"] = groupVec;
            }

            // conn->send(response.dump());

            sendWithLengthPrefix(conn, response);
        }
    }
    else
    {
        // 该用户不存在
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;                   // 业务失败
        response["errmsg"] = "用户名或密码错误"; // 业务失败

        // conn->send(response.dump());

        sendWithLengthPrefix(conn, response);
    }
}

// 处理注册业务 name passwd
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0; // 业务成功
        response["id"] = user.getId();
        // conn->send(response.dump());
        sendWithLengthPrefix(conn, response);
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1; // 业务失败
        // conn->send(response.dump());
        sendWithLengthPrefix(conn, response);
    }
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int user_id = js["id"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(user_id);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user_id);

    // 更新用户的状态信息
    User user(user_id, "", "", "offline");
    _userModel.updateState(user);
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                // 从map表删除用户的连接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId());

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int toId = js["to"].get<int>();

    // 访问连接信息表，必须保证线程安全
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toId);
        if (it != _userConnMap.end())
        {
            // it->second->send(js.dump());
            // return;

            sendWithLengthPrefix(it->second, js);
            return;
        }
    }

    // 查询toid是否在线(通过Redis跨服务器通信场景)
    User user = _userModel.query(toId);
    if (user.getState() == "online")
    {
        // // 生成 JSON 字符串并打印（验证是否有效）
        // std::string json_str = js.dump();
        // std::cout << "准备发布到 Redis 的消息：" << json_str << std::endl;
        // std::cout << "消息长度:" << json_str.size() << " 字节" << std::endl;
        // std::cout << "消息前5个字符:" << json_str.substr(0, 5) << std::endl; // 确认开头是否为 '{'

        // 注意：Redis发布的是原始JSON字符串（不含长度前缀），
        // 接收方服务器从Redis订阅消息后，仍需按上述逻辑添加长度前缀再发送给客户端
        _redis.publish(toId, js.dump());
        return;
    }

    // toId不在线，离线消息(存储原始JSON字符串，用户上线时添加长度前缀发送)
    _offlineMsgModel.insert(toId, js.dump());
}

// 添加好友业务  msgid user_id friend_id
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int user_id = js["id"].get<int>();
    int friend_id = js["friend_id"].get<int>();

    // 存储好友信息
    _friendModel.insert(user_id, friend_id);
}

// 创建群组业务
void ChatService::creatGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // group_id是数据库自动生成的，这里的id是创建数据库的用户的id
    int user_id = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.creatGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(group.getId(), user_id, CREATOR);
    }
}

// 加入群组业务
void ChatService::addToGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int user_id = js["id"].get<int>();
    int group_id = js["group_id"].get<int>();
    _groupModel.addGroup(group_id, user_id, NORMAL);
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int user_id = js["id"].get<int>();
    int group_id = js["group_id"].get<int>();
    vector<int> user_idVec = _groupModel.queryGroupUsers(user_id, group_id);

    lock_guard<mutex> lock(_connMutex);
    for (int id : user_idVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发消息
            // it->second->send(js.dump());

            sendWithLengthPrefix(it->second, js);
        }
        else
        {
            // 查询id是否在线
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
                return;
            }
            else
            {
                // 存储离线群消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

// 从redis消息队列中获取订阅的信息
void ChatService::handleRedisSubscribeMessage(int user_id, string msg)
{
    // cout << "【接收方】从Redis订阅到消息：user_id=" << user_id << "，原始消息：" << msg << endl;
    // cout << "【接收方】消息长度：" << msg.size() << "字节" << endl;

    // ====== 步骤1：验证接收到的Redis消息是否为有效JSON ======
    json js;
    try
    {
        js = json::parse(msg); // 解析JSON，若失败直接丢弃
        // cout << "【接收方】解析后的JSON：" << js.dump(4) << endl; // 格式化输出，确认结构正确
    }
    catch (const nlohmann::json::parse_error &e)
    {
        cerr << "Redis消息JSON解析失败：" << e.what() << "，原始消息：" << msg << endl;
        return; // 忽略无效消息，避免转发给客户端导致错误
    }

    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(user_id);
    if (it != _userConnMap.end())
    {
        // it->second->send(msg);
        // return;

        // 关键修改：调用通用发送函数，添加4字节长度前缀
        // (msg 是 JSON 字符串，需先解析为 json 对象，再传入 sendWithLengthPrefix)
        try
        {
            sendWithLengthPrefix(it->second, js); // 调用封装的发送函数（自动添加长度前缀）
        }
        catch (const nlohmann::json::parse_error &e)
        {
            std::cerr << "Redis 消息解析 JSON 失败：" << e.what() << "，原始消息：" << msg << std::endl;
        }
        return;
    }

    // 存储该用户的离线消息，通道转发消息的过程中用户下线
    _offlineMsgModel.insert(user_id, msg);
}

// 通用发送函数：添加4字节长度前缀并发送JSON消息
void ChatService::sendWithLengthPrefix(const TcpConnectionPtr &conn, json &js)
{
    if (!conn || !conn->connected())
    {
        // 连接不存在或已断开，直接返回（避免崩溃）
        cerr << "连接已断开，无法发送消息" << endl;
        return;
    }

    try
    {
        // 1. 将JSON对象转为字符串
        std::string json_str = js.dump();

        // 2. 计算JSON长度（转为网络字节序：4字节无符号整数）
        uint32_t len = htonl(json_str.size()); // 本地字节序→网络字节序（大端）

        // 3. 拼接“4字节长度前缀 + JSON数据”
        std::string send_data;
        send_data.resize(4);            // 预留4字节存储长度前缀
        memcpy(&send_data[0], &len, 4); // 将长度写入前4字节
        send_data += json_str;          // 拼接JSON字符串

        // 4. 通过TcpConnection发送完整数据
        conn->send(send_data);
    }
    catch (const std::exception &e)
    {
        // 捕获JSON序列化或内存操作异常（如json_str过大）
        std::cerr << "发送消息失败：" << e.what() << std::endl;
    }
}