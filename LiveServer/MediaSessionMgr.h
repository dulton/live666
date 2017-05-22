#pragma  once

#include <vector>
#include <string>
#include <map>

using namespace std;
class ServerMediaSession;

/*
�̰߳�ȫ��ServerMediaSession�����ࡣ�ܹ�����,ɾ��,����,���ƿ��,����,��ͣ,����һ���ܵ�ý�������
xml-rpc��rtsp�˿ڶ�ֱ�ӿ��������
*/
class MediaSessionMgr
{
    typedef map<string, ServerMediaSession*> MediaSessionMap_t;
    typedef MediaSessionMap_t::iterator SessionIt_t;
    typedef pair<string, ServerMediaSession*> MediaSessionType;

public:
    void addServerMediaSession(ServerMediaSession* serverMediaSession);

    virtual ServerMediaSession*
        lookupServerMediaSession(char const* streamName, Boolean isFirstLookupInSession = True);

    void removeServerMediaSession(ServerMediaSession* serverMediaSession);

    void removeServerMediaSession(char const* streamName);

    void closeAllClientSessionsForServerMediaSession(ServerMediaSession* serverMediaSession);

    void closeAllClientSessionsForServerMediaSession(char const* streamName);

    void deleteServerMediaSession(ServerMediaSession* serverMediaSession);

    void deleteServerMediaSession(char const* streamName);

    unsigned numClientSessions() const { return m_mMediaSessions->size(); }
private:
    MediaSessionMap_t m_mMediaSessions;
};