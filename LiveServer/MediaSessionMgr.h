#pragma  once

#include <vector>
#include <string>
#include <map>

using namespace std;
class MyServerMediaSession;

/*
线程安全的ServerMediaSession管理类。能够增加,删除,查找,控制快进,播放,暂停,这是一个总的媒体控制类
全局唯一
xml-rpc和rtsp端口都直接控制这个类
*/

#define SESSION_MGR_OK       (0)

//一般错误
#define SESSION_MGR_ERR      (1)

//未知的名称
#define SESSION_MGR_UNKNOWN  (2)

class MediaSessionMgr
{
    typedef map<string, MyServerMediaSession*> MediaSessionMap_t;
    typedef MediaSessionMap_t::iterator SessionIt_t;
    typedef pair<string, MyServerMediaSession*> MediaSessionType;
public:
    Boolean isMediaSessionExist(string Name);

    Boolean generateSDPDescription(string& Sdp);

    //创建一个自定义的MediaSession
    int addServerMediaSession(MyServerMediaSession* serverMediaSession);

    int removeServerMediaSession(string Name);

    int playStream(string Name);

    int stopStream(string Name);

    int seekStream(string Name,string absStart, string absEnd);

    int pauseStream(string Name);
    
    int nullSeekStream(string Name,double streamDuration, u_int64_t& numBytes);

    int seekStream(string Name,double& seekNPT, double streamDuration, u_int64_t& numBytes);

    int setStreamScale(string Name, float scale);

    int getSessionId(string Name);

    float getCurrentNPT();

    int getRtpInfo(string& rtpInfo);
    
    float duration();

    virtual void getStreamParameters(string Name,unsigned clientSessionId, // in
        netAddressBits clientAddress, // in
        Port const& clientRTPPort, // in
        Port const& clientRTCPPort, // in
        int tcpSocketNum, // in (-1 means use UDP, not TCP)
        unsigned char rtpChannelId, // in (used if TCP)
        unsigned char rtcpChannelId, // in (used if TCP)
        netAddressBits& destinationAddress, // in out
        u_int8_t& destinationTTL, // in out
        Boolean& isMulticast, // out
        Port& serverRTPPort, // out
        Port& serverRTCPPort // out
        );

    Boolean authentication(string Name, string uri, string UserName,
        string realm, string nonce,
        string& response);

    string Nonce(string Name);

    string Realm(string Name);

    string NewNonce(string Name);
private:
    virtual MyServerMediaSession*
        lookupServerMediaSession(char const* streamName, Boolean isFirstLookupInSession = True);

    void removeServerMediaSession(MyServerMediaSession* serverMediaSession);

    void deleteServerMediaSession(MyServerMediaSession* serverMediaSession);

    size_t numClientSessions() const { return m_mMediaSessions.size(); }
private:
    MediaSessionMap_t m_mMediaSessions;
};