#ifndef LOCKER_H     // 避免头文件的重定义
#define LOCKER_H


#include <pthread.h>     // 线程头文件
#include <semaphore.h>   // 信号量头文件
#include <exception>     // 异常处理

/*
线程同步机制包装类
===============
多线程同步，确保任一时刻只能有一个线程能进入关键代码段.
> * 信号量
> * 互斥锁
> * 条件变量
*/

// 信号量类
class sem
{
public:
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)  // 无参构造函数，类似于消费者。初始化成功返回0。
        {
            throw std::exception();   // 初始化失败，则抛出异常。
        }
    }
    sem(int num)  // 有参构造函数，类似于生产者。
    {
        if (sem_init(&m_sem, 0, num) != 0)  // 初始化操作，num为信号量的值，成功返回0。
        {
            throw std::exception();  // 初始化失败，则抛出异常。
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);  // 释放信号量资源
    }
    bool wait()  // 等待信号量
    {
        return sem_wait(&m_sem) == 0;   // 对信号量加锁，调用一次对信号量的值-1，如果值为0，就阻塞
    }
    bool post()  // 增加信号量
    {
        return sem_post(&m_sem) == 0;  // 对信号量解锁，调用一次对信号量的值+1
    }

private:
    sem_t m_sem;   // 创建信号量变量
};

// 互斥锁类
class locker
{
public:
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)   // 互斥量的初始化 初始化正常返回0
        {
            throw std::exception();                    // 初始化不正确就抛出异常
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);                // 释放互斥量资源
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;       // 互斥量加锁  加锁正确返回0
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;    // 互斥量解锁   解锁正确返回0
    }
    pthread_mutex_t *get()
    {
        return &m_mutex;            // 获取互斥量
    }

private:
    pthread_mutex_t m_mutex;       // 创建互斥量
};

//  条件变量类
class cond
{
public:
    cond()   // 构造函数
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)  // 条件变量初始化
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();     
        }
    }
    ~cond()  // 析构函数
    {
        pthread_cond_destroy(&m_cond);   // 释放条件变量
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
        // 没有数据，需要等待
        // 当wait调用阻塞的时候，会对互斥锁解锁，让生产者模型生产，当不阻塞的时候，继续执行，会加上锁。
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {   
        // 等待多长时间，调用了这个函数，线程会阻塞，直到指定的时间结束。
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;     // 只要生产一个就通知消费者消费
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;  // 唤醒所有的等待的线程
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;            // 创建条件变量
};

#endif
