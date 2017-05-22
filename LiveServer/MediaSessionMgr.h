#pragma  once

#include <vector>
#include <string>
#include <map>

using namespace std;
class ServerMediaSession;

/*
线程安全的ServerMediaSession管理类。能够增加,删除,查找,控制快进,播放,暂停,这是一个总的媒体控制类
xml-rpc和rtsp端口都直接控制这个类
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