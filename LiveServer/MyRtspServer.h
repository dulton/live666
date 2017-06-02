#pragma  once

#include "Media.hh"
#include "netaddress.hh"

#include "MyThread.h"

#ifndef REQUEST_BUFFER_SIZE
#define REQUEST_BUFFER_SIZE 20000 // for incoming requests
#endif
#ifndef RESPONSE_BUFFER_SIZE
#define RESPONSE_BUFFER_SIZE 20000
#endif

class MyRTSPClientConnection;
class MediaSessionMgr;

class MyRTSPServer: public Medium , public MyThread{
public:

    MyRTSPServer(MediaSessionMgr& MediaMgr,UsageEnvironment& env, Port ourPort);
    virtual ~MyRTSPServer();

    portNumBits httpServerPortNum() const; // in host byte order.  (Returns 0 if not present.)
    virtual int Run();
private: // redefined virtual functions
    virtual Boolean isRTSPServer() const;
protected: // redefined virtual functions
    static void incomingConnectionHandler(void*, int /*mask*/);
    void incomingConnectionHandlerOnSocket();
    int setUpOurSocket(UsageEnvironment& env, Port& ourPort);
private:
    int fHTTPServerSocket; // for optional RTSP-over-HTTP tunneling
    Port fHTTPServerPort; // ditto
    MediaSessionMgr& fMediaMgr;
};
