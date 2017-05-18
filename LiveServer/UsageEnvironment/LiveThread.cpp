#include "LiveThread.h"


LiveThread::LiveThread():m_ThreadState(Thread_Stop),m_f(NULL)
{
	m_par.threadPar = NULL;
	m_par.con = Thread_Stop;
}
LiveThread::~LiveThread()
{
	Stop();
}

//一直等到线程函数开始才返回
void LiveThread::Start(threadFunType f, void* par)
{
	if(m_ThreadState == Thread_Stop)
	{
		m_par.threadPar = par;
		m_par.con = Thread_Run;
		pthread_create(&m_pid,NULL,f, (void*)&m_par);
		m_ThreadState = Thread_Run;
	}
}
void LiveThread::Start(void* par)
{
	Start(m_f, par);
}
void LiveThread::SetThreadFun(threadFunType f)
{
	m_f = f;
}
eThreadState LiveThread::getState()
{
	return m_ThreadState;
}

void LiveThread::Stop()
{
	if(m_ThreadState == Thread_Run)
	{
		m_par.con = Thread_Stop;
		pthread_join(m_pid,NULL);
		m_ThreadState = Thread_Stop;
	}
}

LiveMutex::LiveMutex()
{
	pthread_mutex_init(&m_mutex, NULL);
}
LiveMutex::~LiveMutex()
{
	pthread_mutex_destroy(&m_mutex);
}

void LiveMutex::lock(void)
{
	pthread_mutex_lock(&m_mutex);
}
void LiveMutex::unlock(void)
{
	pthread_mutex_unlock(&m_mutex);
}