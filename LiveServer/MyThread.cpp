#include "MyThread.h"

class MyThreadPrivate
{
public:
    pthread_t pid;
};

void* MyThreadFun(void* pUsr)
{
    MyThread* pMyThread = (MyThread*)pUsr;

    pMyThread->Run();

    pMyThread->m_ThreadState = THREAD_STATE_STOP;

    return NULL;
}


MyThread::MyThread()
{
    UsrWishState = THREAD_STATE_STOP;
    m_ThreadState = THREAD_STATE_STOP;
    m_Private = new MyThreadPrivate;
}

 MyThread::~MyThread()
{
    Stop();
    delete m_Private;
}

void MyThread::Start()
{
    if (m_ThreadState == THREAD_STATE_STOP){
        pthread_create(&m_Private->pid, NULL, MyThreadFun, this);
        UsrWishState = m_ThreadState = THREAD_STATE_RUN;
    }
}

void MyThread::Stop()
{
    if (m_ThreadState == THREAD_STATE_STOP) {
        return;
    }

    UsrWishState = THREAD_STATE_STOP;
    pthread_join(m_Private->pid, NULL);
    m_ThreadState = THREAD_STATE_STOP;
}