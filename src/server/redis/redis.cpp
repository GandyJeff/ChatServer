#include "redis.hpp"
#include <iostream>

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
    if (_publish_context == nullptr)
    {
        cerr << "Failed to connect publish redis!" << endl;
        return false;
    }

    // 负责subscribe订阅消息的上下文连接
    _subscribe_context = redisConnect("127.0.0.1", 6379);
    if (_subscribe_context == nullptr)
    {
        cerr << "Failed to connect subscribe redis!" << endl;
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
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message);
    if (reply == nullptr)
    {
        cerr << "Failed to reply publish command!" << endl;
        return false;
    }
    freeReplyObject(reply);
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
        // 订阅收到的消息是一个带三元素的数组
        if (reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr)
        {
            // 给业务层上报通道上发生的消息
            _notify_message_handler(atoi(reply->element[1]->str), reply->element[2]->str);
        }
        freeReplyObject(reply);
    }

    cerr << ">>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<" << endl;
}

// 初始化向业务层上报通道消息的回调对象
void Redis::init_notify_handler(function<void(int, string)> fn)
{
    this->_notify_message_handler = fn;
}