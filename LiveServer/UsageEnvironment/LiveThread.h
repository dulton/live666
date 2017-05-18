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
	eThreadState con;//ͨ��������������߳�
	void* threadPar;
};

//�̺߳����ڲ�����s,pΪʵ�ʲ���
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
    //���ֵ��Ӧ�߳��������״̬S1
    eThreadState m_ThreadState;
    pthread_t m_pid;
    //���ֵ���߳��������̼߳���S2
    //֮���Բ���ֱ��S1,��ΪS2��Ϊֹͣ���N�������ͣ����,��N�������߳�״̬�������е�
    LiveThreadPar m_par;
    threadFunType m_f;
};


#endif