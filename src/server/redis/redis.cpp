#include "redis.hpp"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <string>
#include <mutex>

Redis::Redis()
    : _publish_context(nullptr), _subscribe_context(nullptr)
{
}

Redis::~Redis()
{
    if (_publish_context != nullptr)
    {
        redisFree(_publish_context);
    }

    if (_subscribe_context != nullptr)
    {
        redisFree(_subscribe_context);
    }
}

// 连接redis服务器
bool Redis::connect()
{
    // 负责publish发布消息的上下文连接
    _publish_context = redisConnect("127.0.0.1", 6379);
    if (_publish_context == nullptr || _publish_context->err != 0)
    {
        cerr << "Failed to connect publish redis! err: "
             << (_publish_context ? _publish_context->errstr : "nullptr") << endl;
        return false;
    }

    // 负责subscribe订阅消息的上下文连接
    _subscribe_context = redisConnect("127.0.0.1", 6379);
    if (_subscribe_context == nullptr || _subscribe_context->err != 0)
    {
        cerr << "Failed to connect subscribe redis! err: "
             << (_subscribe_context ? _subscribe_context->errstr : "nullptr") << endl;
        return false;
    }

    // 在单独的线程中，监听通道上的事件，有消息给业务层进行上报
    thread t([&]()
             { observer_channel_message(); });
    t.detach();

    cout << "connect redis-server success!" << endl;

    return true;
}

// 向redis指定的通道channel发布消息
bool Redis::publish(int channel, string message)
{
    // ====== 步骤1：验证发布上下文是否有效（避免连接异常导致发送失败）======
    if (_publish_context == nullptr || _publish_context->err != 0)
    {
        cerr << "【发布失败】Redis 发布上下文错误：" << (_publish_context ? _publish_context->errstr : "nullptr") << endl;
        return false;
    }

    // // ====== 步骤2：打印发布前的消息（确认消息正常）======
    // std::cout << "【发布前消息】长度：" << message.size() << "字节" << std::endl;
    // std::cout << "【发布前字符串】：" << message << std::endl;

    // redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message);

    // ====== 步骤3：使用 %b 格式符发布（二进制安全，避免截断和格式符解析错误）======
    // 格式：PUBLISH <channel> <message>，其中 %b 表示二进制数据（data, len）
    redisReply *reply = (redisReply *)redisCommand(
        _publish_context,
        "PUBLISH %d %b", // %d：通道号（整数），%b：二进制数据（安全处理任意字符）
        channel,         // 参数1：通道号（int）
        message.c_str(), // 参数2：消息数据（const char*，二进制安全）
        message.size()   // 参数3：消息长度（size_t，显式指定长度，避免 \0 截断）
    );

    // ====== 步骤4：检查发布结果======
    if (reply == nullptr)
    {
        cerr << "Failed to reply publish command!" << endl;
        return false;
    }

    if (reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr << "【发布失败】Redis 错误：" << reply->str << std::endl;
        freeReplyObject(reply);
        return false;
    }

    // ====== 步骤5：释放回复并返回成功======
    freeReplyObject(reply);
    cout << "【发布成功】消息已发送到 Redis 通道 " << channel << endl;
    return true;
}

// 向redis指定的通道subscribe订阅消息
bool Redis::subscribe(int channel)
{
    // redisCommand = redisAppendCommand + redisBufferWrite + redisGetReply
    // redisAppendCommand把命令写到本地缓存
    // redisBufferWrite从本地缓存把命令发送到redis server
    // redisGetReply以阻塞的方式等待远端的响应

    // SUBSCRIBE命令本身会造成线程阻塞等待通道里面发生消息，这里只做订阅通道，不接收通道消息
    // 通道消息的接收专门在observer_channel_messagel函数中的独立线程中进行
    // 只负责发送命令，不阻塞接收redis server响应消息，否则和notifyMsg线程抢占响应资源

    std::lock_guard<std::mutex> lock(_subscribe_mutex); // 加锁

    // 若上下文存在错误，尝试重置连接
    if (_subscribe_context != nullptr && _subscribe_context->err != 0)
    {
        cerr << "Resetting subscribe context due to error: " << _subscribe_context->errstr << endl;
        redisFree(_subscribe_context);
        _subscribe_context = redisConnect("127.0.0.1", 6379);
        if (_subscribe_context == nullptr || _subscribe_context->err != 0)
        {
            cerr << "Failed to reconnect subscribe redis!" << endl;
            return false;
        }
    }

    if (redisAppendCommand(this->_subscribe_context, "SUBSCRIBE %d", channel) == REDIS_ERR)
    {
        cerr << "Failed to reply subscribe command!" << endl;
        return false;
    }

    // redisBufferWritei可以循环发送缓冲区，直到缓冲区数据发送完毕(done被置为1)
    int done = 0;
    while (!done)
    {
        if (redisBufferWrite(this->_subscribe_context, &done) == REDIS_ERR)
        {
            cerr << "Failed to reply subscribe command!" << endl;
            return false;
        }
    }
    // redisGetReply

    return true;
}

// 向redis指定的通道unsubscribe取消订阅消息
bool Redis::unsubscribe(int channel)
{
    std::lock_guard<std::mutex> lock(_subscribe_mutex); // 加锁
    if (redisAppendCommand(this->_subscribe_context, "UNSUBSCRIBE %d", channel) == REDIS_ERR)
    {
        cerr << "Failed to reply unsubscribe command!" << endl;
        return false;
    }

    // redisBufferWritei可以循环发送缓冲区，直到缓冲区数据发送完毕(done被置为1)
    int done = 0;
    while (!done)
    {
        if (redisBufferWrite(this->_subscribe_context, &done) == REDIS_ERR)
        {
            cerr << "Failed to reply unsubscribe command!" << endl;
            return false;
        }
    }
    // redisGetReply

    return true;
}

// 在独立线程中接收订阅通道中的消息，响应消息
void Redis::observer_channel_message()
{
    redisReply *reply = nullptr;
    while (redisGetReply(this->_subscribe_context, (void **)&reply) == REDIS_OK)
    {
        // 严格检查Redis回复结构：数组类型+3个元素+类型为"message"
        if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY || reply->elements != 3)
        {
            freeReplyObject(reply);
            continue;
        }

        redisReply *type_elem = reply->element[0];    // 第一个元素："message"
        redisReply *channel_elem = reply->element[1]; // 第二个元素：通道名（user_id）
        redisReply *msg_elem = reply->element[2];     // 第三个元素：消息内容（JSON字符串）

        // 验证元素类型和内容有效性
        if (type_elem->type != REDIS_REPLY_STRING || strcmp(type_elem->str, "message") != 0 ||
            channel_elem->type != REDIS_REPLY_STRING || channel_elem->str == nullptr ||
            msg_elem->type != REDIS_REPLY_STRING || msg_elem->str == nullptr)
        {
            cerr << "无效的Redis订阅消息结构" << endl;
            freeReplyObject(reply);
            continue;
        }

        // 提取用户ID和消息内容（按长度读取，二进制安全）
        int user_id = atoi(channel_elem->str);
        string message(msg_elem->str, msg_elem->len); // 关键：用msg_elem->len确保完整读取

        // 调用业务层处理函数（转发给客户端）
        _notify_message_handler(user_id, message);

        // // 订阅收到的消息是一个带三元素的数组
        // if (reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr)
        // {
        //     // 给业务层上报通道上发生的消息
        //     _notify_message_handler(atoi(reply->element[1]->str), reply->element[2]->str);
        // }

        // // 打印消息元素的原始字节（十六进制）
        // redisReply *msg_elem = reply->element[2];
        // std::cout << "消息原始字节（十六进制）：";
        // for (size_t i = 0; i < msg_elem->len; ++i)
        // {
        //     std::cout << std::hex << setw(2) << setfill('0')
        //               << static_cast<int>(static_cast<unsigned char>(msg_elem->str[i])) << " ";
        // }
        // std::cout << std::dec << "（长度：" << msg_elem->len << "字节）" << std::endl;

        freeReplyObject(reply);
    }

    cerr << ">>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<" << endl;
}

// 初始化向业务层上报通道消息的回调对象
void Redis::init_notify_handler(function<void(int, string)> fn)
{
    this->_notify_message_handler = fn;
}