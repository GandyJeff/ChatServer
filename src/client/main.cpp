#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <thread>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <semaphore.h>
#include <atomic>

#include "json.hpp"
using json = nlohmann::json;

#include "public.hpp"
#include "user.hpp"
#include "group.hpp"

// 记录当前系统登录的用户信息
User _currentUser;
// 记录当前登录用户的好友列表信息
vector<User> _currentUserFrientList;
// 记录当前登录用户的群组列表信息
vector<Group> _currentUserGroupList;

// 控制主菜单页面程序
bool isMainMenuRunning = false;

// 用于读写线程之间的通信
sem_t rwsem;
// 记录登录状态
atomic_bool _isLoginSuccess{false};

// 接受线程
void readTaskHandler(int client_fd);
// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime();
// 主聊天页面程序
void mainMenu(int);
// 显示当前登录成功用户的基本信息
void showCurrentUserData();

// 聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "Command invalid! Example:./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1)
    {
        cerr << "Failed to creat socket!" << endl;
        exit(-1);
    }

    // 填写client需要连接的server信息ip+port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        cerr << "Failed to connect server!" << endl;
        close(client_fd);
        exit(-1);
    }

    // 初始化读写线程通信用的信号量
    sem_init(&rwsem, 0, 0);

    // 连接服务器成功，启动接收子线程
    std::thread readTask(readTaskHandler, client_fd);
    readTask.detach();

    // main线程用于接收用户输入，负责发送数据
    for (;;)
    {
        // 显示首页面菜单登录、注册、退出
        cout << "========================" << endl;
        cout << "1.login" << endl;
        cout << "2.register" << endl;
        cout << "3.quit" << endl;
        cout << "========================" << endl;
        cout << "choice:";

        int choice = 0;
        cin >> choice;
        cin.get(); // 读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: // login业务
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "user_id:";
            cin >> id;
            cin.get();
            cout << "password:";
            cin.getline(pwd, sizeof(pwd));

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            _isLoginSuccess = false;

            ssize_t len = send(client_fd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "Failed to send login msg:" << request << endl;
            }

            // 等待信号量，由子线程处理完登录的响应消息后，通知这里
            sem_wait(&rwsem);

            if (_isLoginSuccess)
            {
                // 进入聊天主菜单页面
                isMainMenuRunning = true;
                mainMenu(client_fd);
            }
        }
        break;
        case 2: // register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, sizeof(name)); // cin >> 和scanf遇到空格自动结束，不使用
            cout << "password:";
            cin.getline(pwd, sizeof(pwd));

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();

            ssize_t len = send(client_fd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "Failed to send reg msg!" << endl;
            }

            // 等待信号量，由子线程处理完注册的响应消息后，通知这里
            sem_wait(&rwsem);
        }
        break;
        case 3: // quit业务
            close(client_fd);
            sem_destroy(&rwsem);
            exit(0);
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }
    return 0;
}

// 处理登录响应的业务逻辑
void doLoginResponse(json &response_js)
{
    if (response_js["errno"].get<int>() != 0) // 登录失败
    {
        cerr << response_js["errmsg"] << endl;
        _isLoginSuccess = false;
    }
    else // 登录成功
    {
        // 全局变量记录当前用户的id和name
        _currentUser.setId(response_js["id"].get<int>());
        _currentUser.setName(response_js["name"]);

        // 记录当前用户的好友列表信息
        if (response_js.contains("friends"))
        {
            // 初始化
            _currentUserFrientList.clear();

            vector<string> vec = response_js["friends"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                User user;
                user.setId(js["id"].get<int>());
                user.setName(js["name"]);
                user.setState(js["state"]);
                _currentUserFrientList.push_back(user);
            }
        }

        // 记录当前用户的群组列表信息
        if (response_js.contains("groups"))
        {
            // 初始化
            _currentUserGroupList.clear();

            vector<string> vec1 = response_js["groups"];
            for (string &groupstr : vec1)
            {
                json group_js = json::parse(groupstr);
                Group group;
                group.setId(group_js["id"].get<int>());
                group.setName(group_js["groupname"]);
                group.setDesc(group_js["groupdesc"]);

                vector<string> vec2 = response_js["users"];
                for (string &userstr : vec2)
                {
                    json user_js = json::parse(userstr);
                    GroupUser user;
                    user.setId(user_js["id"].get<int>());
                    user.setName(user_js["name"]);
                    user.setState(user_js["state"]);
                    user.setRole(user_js["role"]);
                    group.getGroupUser().push_back(user);
                }

                _currentUserGroupList.push_back(group);
            }
        }

        // 显示当前登录用户的基本信息
        showCurrentUserData();

        // 显示当前用户的离线消息、个人聊天信息或者群组消息
        if (response_js.contains("offlinemsg"))
        {
            vector<string> vec = response_js["offlinemsg"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                // time + [id] + name + "said:" + xxx
                if (js["msgid"].get<int>() == ONE_CHAT_MSG)
                {
                    cout << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>()
                         << " said:" << js["msg"].get<string>() << endl;
                }
                else
                {
                    cout << "群消息[" << js["group_id"] << "]: " << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>()
                         << " said:" << js["msg"].get<string>() << endl;
                }
            }
        }
        _isLoginSuccess = true;
    }
}

// 处理注册响应的业务逻辑
void doRegResponse(json &response_js)
{
    if (response_js["errno"].get<int>() != 0) // 注册失败
    {
        cerr << "name is already exist,register error!" << endl;
    }
    else
    {
        cerr << "name register success,user_id is " << response_js["id"]
             << ",do not forget it!" << endl;
    }
}

// 接受线程
void readTaskHandler(int client_fd)
{
    for (;;)
    {
        char buffer[1024] = {0};
        int bytes_recv = recv(client_fd, buffer, sizeof(buffer), 0); // 阻塞
        if (bytes_recv == -1 || bytes_recv == 0)
        {
            close(client_fd);
            exit(-1);
        }

        // 接收ChatServer转发的数据，反序列化生成json数据对象
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        if (msgtype == ONE_CHAT_MSG)
        {
            cout << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>()
                 << " said:" << js["msg"].get<string>() << endl;
            continue;
        }

        if (msgtype == GROUP_CHAT_MSG)
        {
            cout << "群消息[" << js["group_id"].get<int>() << "]: " << js["time"].get<string>() << " [" << js["id"] << "] " << js["name"].get<string>()
                 << " said:" << js["msg"].get<string>() << endl;
            continue;
        }

        if (msgtype == LOGIN_MSG_ACK)
        {

            doLoginResponse(js); // 处理登录响应的业务逻辑
            sem_post(&rwsem);    // 通知主线程，登录结果处理完成
            continue;
        }

        if (msgtype == REG_MSG_ACK)
        {

            doRegResponse(js); // 处理注册响应的业务逻辑
            sem_post(&rwsem);  // 通知主线程，注册结果处理完成
            continue;
        }
    }
}

// "help" command handler
void help(int client_fd = 0, string str = "");

// "addfriend" command handler
void addfriend(int, string);

// "chat" command handler
void chat(int, string);

// "creategroup" command handler
void creategroup(int, string);

// "addgroup" command handler
void addgroup(int, string);

// "groupchat" command handler
void groupchat(int, string);

// "loginout" command handler
void loginout(int, string);

// // 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}

// 系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令,格式help"},
    {"chat", "一对一聊天,格式chat:friend_id:message"},
    {"addfriend", "添加好友,格式addfriend:friend_id"},
    {"creategroup", "创建群组,格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组,格式addgroup:group_id"},
    {"groupchat", "群聊,格式groupchat:group_id:message"},
    {"loginout", "注销,格式loginout"}};

// 注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}};

// // 主聊天页面程序
void mainMenu(int client_fd)
{
    help();

    char buffer[1024] = {0};

    while (isMainMenuRunning)
    {
        cin.getline(buffer, sizeof(buffer));
        string commandbuf(buffer);
        string command;                   // 存储命令
        int index = commandbuf.find(":"); // 返回冒号的下标
        if (index == -1)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, index);
        }
        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        // 调用相应命令的事件处理回调，mainMenu对修改封闭，添加新功能不需要修改该函数
        it->second(client_fd, commandbuf.substr(index + 1, commandbuf.size() - index)); // 返回除了命令以外,冒号后面的内容
    }
}

// "help" command handler
void help(int, string)
{
    cout << "Show command list >>> " << endl;
    for (auto &p : commandMap)
    {
        cout << p.first << ":" << p.second << endl;
    }
    cout << endl;
}

// "addfriend" command handler
void addfriend(int client_fd, string str)
{
    int friend_id = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = _currentUser.getId();
    js["friend_id"] = friend_id;
    string buffer = js.dump();

    int len = send(client_fd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "Failed to send addfriend msg -> " << buffer << endl;
    }
}

// "chat" command handler
void chat(int client_fd, string str)
{
    int index = str.find(":"); // friend_id:message
    if (index == -1)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friend_id = atoi(str.substr(0, index).c_str());
    string message = str.substr(index + 1, str.size() - index);

    // 禁止发送给自己
    if (friend_id != _currentUser.getId())
    {
        json js;
        js["msgid"] = ONE_CHAT_MSG;
        js["id"] = _currentUser.getId();
        js["name"] = _currentUser.getName();
        js["to"] = friend_id;
        js["msg"] = message;
        js["time"] = getCurrentTime();
        string buffer = js.dump();

        int len = send(client_fd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
        if (len == -1)
        {
            cerr << "Failed to send chat msg -> " << buffer << endl;
        }
    }
    else
    {
        cerr << "You can not chat with yourself!" << endl;
        return;
    }
}
// "creategroup" command handler
void creategroup(int client_fd, string str)
{
    int index = str.find(":"); // groupname:groupdesc
    if (index == -1)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    string groupname = str.substr(0, index);
    string groupdesc = str.substr(index + 1, str.size() - index);

    json js;
    js["msgid"] = CREAT_GROUP_MSG;
    js["id"] = _currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(client_fd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "Failed to send creategroup msg -> " << buffer << endl;
    }
}

// "addgroup" command handler
void addgroup(int client_fd, string str)
{
    // group_id
    int group_id = atoi(str.c_str());

    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = _currentUser.getId();
    js["group_id"] = group_id;
    string buffer = js.dump();

    int len = send(client_fd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "Failed to send addgroup msg -> " << buffer << endl;
    }
}

// "groupchat" command handler
void groupchat(int client_fd, string str)
{
    int index = str.find(":"); // group_id:message
    if (index == -1)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int group_id = atoi(str.substr(0, index).c_str());
    string message = str.substr(index + 1, str.size() - index);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = _currentUser.getId();
    js["name"] = _currentUser.getName();
    js["group_id"] = group_id;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(client_fd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "Failed to send groupchat msg -> " << buffer << endl;
    }
}

// "loginout" command handler
void loginout(int client_fd, string str)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = _currentUser.getId();
    string buffer = js.dump();

    int len = send(client_fd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (len == -1)
    {
        cerr << "Failed to send loginout msg -> " << buffer << endl;
    }
    else
    {
        isMainMenuRunning = false;
    }
}

// 显示当前登录用户的基本信息
void showCurrentUserData()
{
    cout << "===================login user================== " << endl;
    cout << "current login user => id:" << _currentUser.getId() << " name:" << _currentUser.getName() << endl;
    cout << "-------------------friend list----------------- " << endl;
    if (!_currentUserFrientList.empty())
    {
        for (User &user : _currentUserFrientList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "-------------------group list------------------ " << endl;
    if (!_currentUserGroupList.empty())
    {
        for (Group &group : _currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getGroupUser())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState() << " " << user.getRole() << endl;
            }
        }
    }
    cout << "=============================================== " << endl;
    cout << endl;
}
