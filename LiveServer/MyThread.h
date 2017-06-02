#pragma  once

#include "MyThread.h"

#include <pthread.h>

#define THREAD_STATE_RUN	(0)
#define THREAD_STATE_STOP	(1)


class MyThreadPrivate;

class MyThread
{
    friend void* MyThreadFun(void* pUsr);
public:
    MyThread();

    virtual ~MyThread();

    virtual void Start();

    int ShouldExit()
    {
        return UsrWishState == THREAD_STATE_STOP;
    }
    int GetState() { return m_ThreadState; }

    virtual int Run() = 0;

    virtual void Stop();

private:
    int m_ThreadState;
    MyThreadPrivate* m_Private;
    int UsrWishState;
};