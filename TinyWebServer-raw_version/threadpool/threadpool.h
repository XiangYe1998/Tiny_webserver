#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"

#include "../CGImysql/sql_connection_pool.h"   // 数据库连接池的头文件


// 线程池类, 定义成模板类是为了代码的复用，模板参数T是任务类
template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    // connPool是数据库连接池指针
    threadpool(connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request);   // 向请求队列中插入任务请求 

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);   // 子线程执行的 静态 函数。
    void run();        // 调用run进行线程工作

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列

    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理。调用无参构造。

    bool m_stop;                //是否结束线程
    connection_pool *m_connPool;  //数据库连接池
};


// 构造函数。创建线程池数组。
template <typename T>
threadpool<T>::threadpool( connection_pool *connPool, int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    // 线程ID初始化
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();

    
    // 创建thread_num个线程，并将他们设置为线程分离
    // 创建线程时执行worker
    for (int i = 0; i < thread_number; ++i)
    {
        //printf("create the %dth thread\n",i);
        // 将类的对象作为参数传递给静态函数，在静态函数中引用这个对象，并对用其动态方法run
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) // 判断创建线程释放成功，成功返回0.
        {
            delete[] m_threads;
            throw std::exception();
        }
        // 被分离的线程在终止的时候，会自动释放资源返回给系统。
        if (pthread_detach(m_threads[i]))  // 判断是否线程分离成功，成功返回0.
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 析构函数。释放线程数组资源，并将结束线程标志设置为true。
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

// 向请求队列中添加任务
// 添加请求任务前，先上锁，添加任务后，再解锁。每增加一个任务，信号量就 +1。
template <typename T>
bool threadpool<T>::append(T *request)
{
    m_queuelocker.lock();   // 加锁
    if (m_workqueue.size() > m_max_requests)  //    请求队列的大小 > 队列中允许的最大请求数
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);  // 往请求队列中插入请求
    m_queuelocker.unlock();  // 解锁
    m_queuestat.post();      // 信号量 +1
    return true;
}

// 子线程执行的代码。调用run使得线程工作。
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    // 将参数强转为线程池类，调用成员方法
    threadpool *pool = (threadpool *)arg;  // 将传进来的本类对象 传如worker中。将这个对象转换为threadpool *。
    pool->run();
    return pool;
}

// 线程池中的线程处理请求队列中的请求。
template <typename T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        m_queuestat.wait();   // 等待信号量，为0阻塞。就是判断请求队列中 是否 有请求。
        m_queuelocker.lock();  // 被唤醒后先加上互斥锁。
        if (m_workqueue.empty())  // 判断请求队列是否为空。
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();  // 从头部取出请求队列中的请求
        m_workqueue.pop_front();     // 取出后从头部弹出
        m_queuelocker.unlock();      // 解锁。
        if (!request)
            continue;

        // 从连接池中取出一个数据库连接
        connectionRAII mysqlcon(&request->mysql, m_connPool);
        
        request->process();     // 线程工作

        // 将数据库连接放回连接池
        // m_connPool->ReleaseConnection(request->mysql);
    }
}
#endif
