# ChatServer
基于muduo网络库实现的可以工作在nginx tcp负载均衡环境中的集群聊天服务器和客户端源码

配置nginx负载均衡，实现支持tcp协议：
**@ubuntu:/ChatServer$ vim /usr/local/nginx/conf/nginx.conf

#nginx tcp loadbalance config

stream{

    upstream MyServer{
        server 127.0.0.1:6000 weight=1 max_fails=3 fail_timeout=30s;
        server 127.0.0.1:6002 weight=1 max_fails=3 fail_timeout=30s;
    }

    server{
        proxy_connect_timeout 1s;
        listen 8000;
        proxy_pass MyServer;
        tcp_nodelay on;
    }
}

运行脚本：
**@ubuntu:/ChatServer$ ./autobuild.sh

启动服务器：
**@ubuntu:/ChatServer$ cd ./bin

**@ubuntu:/ChatServer/bin$ ./ChatServer 127.0.0.1 6000

**@ubuntu:/ChatServer/bin$ ./ChatServer 127.0.0.1 6002

启动客户端：
**@ubuntu:/ChatServer/bin$ ./ChatClient 127.0.0.1 8000


