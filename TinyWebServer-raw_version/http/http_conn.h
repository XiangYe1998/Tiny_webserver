#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

class http_conn
{
public:
    //设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
    //设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
    //设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;

    //报文的请求方法，本项目只用到GET和POST
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}     // 构造函数
    ~http_conn() {}    // 析构函数

public:
    void init(int sockfd, const sockaddr_in &addr); // 初始化新接受的连接
    void close_conn(bool real_close = true);        // 关闭http连接
    void process();                                 // 处理客户端请求
    bool read_once();                               // 非阻塞读，读取浏览器发来的全部数据
    bool write();                                   // 非阻塞写，响应报文写入函数
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    // 同步线程初始化数据库读取表
    void initmysql_result(connection_pool *connPool);

    // CGI使用线程池初始化数据库表
    // void initresultFile(connection_pool *connPool);

private:
    void init();   // 初始化连接
    HTTP_CODE process_read();   // 从m_read_buf读取，并处理请求报文
    bool process_write(HTTP_CODE ret);  // 向m_write_buf写入响应报文数据
    
    // 下面这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line(char *text);   // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_headers(char *text);        // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_content(char *text);        // 主状态机解析报文中的请求内容
    HTTP_CODE do_request();                     // 生成响应报文
    
    // get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();                   // 从状态机读取一行，分析是请求报文的哪一部分

    void unmap();
    
    // 根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;  // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int m_user_count; // 统计用户的数量
    MYSQL *mysql;

private:
    // 该HTTP连接的socket和对方的socket地址
    int m_sockfd;             
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];   // 存储读取的请求报文数据
    int m_read_idx;                      // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    int m_checked_idx;                   // 当前正在分析的字符在读缓冲区中的位置
    int m_start_line;                    // 当前正在解析的行的起始位置
    
    char m_write_buf[WRITE_BUFFER_SIZE];  // 写缓冲区
    int m_write_idx;                      // 写缓冲区中待发送的字节数
    
    CHECK_STATE m_check_state;     // 主状态机当前所处的状态
    METHOD m_method;               // 请求方法
    
    // 以下为解析请求报文中对应的6个变量
    // 存储读取文件的名称
    char m_real_file[FILENAME_LEN]; // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char *m_url;                    // 客户请求的目标文件的文件名
    char *m_version;                // HTTP协议版本号，我们仅支持HTTP1.1
    char *m_host;                   // 主机名
    int m_content_length;           // HTTP请求的消息总长度
    bool m_linger;                  // HTTP请求是否要求保持连接
    
    char *m_file_address;      // 读取服务器上的文件地址
    struct stat m_file_stat;
    struct iovec m_iv[2];      // io向量机制iovec
    int m_iv_count;
    int cgi;                   // 是否启用的POST
    char *m_string;            // 存储请求头数据
    
    int bytes_to_send;         // 剩余发送字节数
    int bytes_have_send;       // 已经发送字节数
};

#endif
