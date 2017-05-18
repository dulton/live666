#ifndef _AV_THREAD_H_
#define _AV_THREAD_H_
#include <pthread.h>

enum eThreadState
{
	Thread_Run,
	Thread_Stop,
};

struct LiveThreadPar 
{
	eThreadState con;//通过这个变量控制线程
	void* threadPar;
};

//线程函数内部监视s,p为实际参数
typedef void* (*threadFunType)(void* run);

class LiveMutex
{
public:
	LiveMutex();
	~LiveMutex();
	void lock(void);
	void unlock(void);
protected:
private:
	pthread_mutex_t m_mutex;
};
class LiveThread
{
public:
	LiveThread();
	~LiveThread();
	void Start(threadFunType f, void* par);
	void Start(void * par);
	void Stop();
	void SetThreadFun(threadFunType f);
	eThreadState getState();
private:
    //这个值反应线程类的现在状态S1
    eThreadState m_ThreadState;
    pthread_t m_pid;
    //这个值给线程用于让线程监视S2
    //之所以不用直接S1,因为S2设为停止后过N毫秒才能停下来,这N毫秒内线程状态还是运行的
    LiveThreadPar m_par;
    threadFunType m_f;
};


#endif