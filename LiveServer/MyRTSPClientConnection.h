#pragma once

#include <vector>
#include <string>

#include "usageenvironment.hh"

using namespace std;

#define RTSP_BUFFER_SIZE 20000 // for incoming requests, and outgoing responses

class MediaSessionMgr;

class MyRTSPClientConnection
{
public:
    MyRTSPClientConnection(MediaSessionMgr& SessinMgr,
        int clientSocket, struct sockaddr_in clientAddr);
    virtual ~MyRTSPClientConnection();

protected:
    virtual void handleRequestBytes(int newBytesRead);
    // Make the handler functions for each command virtual, to allow subclasses to reimplement them, if necessary:
    virtual void handleCmd_OPTIONS();
    // You probably won't need to subclass/reimplement this function; reimplement "RTSPServer::allowedCommandNames()" instead.
    virtual void handleCmd_GET_PARAMETER(char const* fullRequestStr); // when operating on the entire server
    virtual void handleCmd_SET_PARAMETER(char const* fullRequestStr); // when operating on the entire server
    virtual void handleCmd_DESCRIBE(char const* urlPreSuffix, char const* urlSuffix, char const* fullRequestStr);
    virtual void handleCmd_REGISTER(char const* cmd/*"REGISTER" or "DEREGISTER"*/,
        char const* url, char const* urlSuffix, char const* fullRequestStr,
        Boolean reuseConnection, Boolean deliverViaTCP, char const* proxyURLSuffix);
    // You probably won't need to subclass/reimplement this function;
    //     reimplement "RTSPServer::weImplementREGISTER()" and "RTSPServer::implementCmd_REGISTER()" instead.
    virtual void handleCmd_bad();
    virtual void handleCmd_notSupported();
    virtual void handleCmd_notFound();
    virtual void handleCmd_sessionNotFound();
    virtual void handleCmd_unsupportedTransport();
    // Support for optional RTSP-over-HTTP tunneling:
    virtual Boolean parseHTTPRequestString(char* resultCmdName, unsigned resultCmdNameMaxSize,
        char* urlSuffix, unsigned urlSuffixMaxSize,
        char* sessionCookie, unsigned sessionCookieMaxSize,
        char* acceptStr, unsigned acceptStrMaxSize);
    virtual void handleHTTPCmd_notSupported(); 
    virtual void handleHTTPCmd_notFound();
    virtual void handleHTTPCmd_OPTIONS();
    virtual void handleHTTPCmd_TunnelingGET(char const* sessionCookie);
    virtual Boolean handleHTTPCmd_TunnelingPOST(char const* sessionCookie, unsigned char const* extraData, unsigned extraDataSize);
    virtual void handleHTTPCmd_StreamingGET(char const* urlSuffix, char const* fullRequestStr);
    // Make the handler functions for each command virtual, to allow subclasses to redefine them:
    virtual void handleCmd_SETUP(char const* urlPreSuffix, char const* urlSuffix, char const* fullRequestStr);
    virtual void handleCmd_withinSession(char const* cmdName,
        char const* urlPreSuffix, char const* urlSuffix,
        char const* fullRequestStr);
    virtual void handleCmd_TEARDOWN();
    virtual void handleCmd_PLAY(char const* fullRequestStr);
    virtual void handleCmd_PAUSE();
    void deleteStreamByTrack(unsigned trackNum);
    Boolean isMulticast() const { return fIsMulticast; }
protected:
    static void incomingRequestHandler(void*, int /*mask*/);

    void resetRequestBuffer();
    void closeSocketsRTSP();
    static void handleAlternativeRequestByte(void*, u_int8_t requestByte);
    void handleAlternativeRequestByte1(u_int8_t requestByte);
    Boolean authenticationOK(char const* cmdName, char const* urlSuffix, char const* fullRequestStr);
    void changeClientInputSocket(int newSocketNum, unsigned char const* extraData, unsigned extraDataSize);
    void incomingRequestHandler1();
    UsageEnvironment& envir() { return *fEnv; }
    void closeSockets();
    // Shortcuts for setting up a RTSP response (prior to sending it):
    void setRTSPResponse(char const* responseStr);
    void setRTSPResponse(char const* responseStr, u_int32_t sessionId);
    void setRTSPResponse(char const* responseStr, char const* contentStr);
    void setRTSPResponse(char const* responseStr, u_int32_t sessionId, char const* contentStr);

    Boolean fIsActive;
    int fClientInputSocket, fClientOutputSocket;
    struct sockaddr_in fClientAddr;
    unsigned char fRequestBuffer[RTSP_BUFFER_SIZE];
    unsigned fRequestBytesAlreadySeen, fRequestBufferBytesLeft;
    unsigned char* fLastCRLF;
    unsigned char fResponseBuffer[RTSP_BUFFER_SIZE];
    unsigned fRecursionCount;
    char const* fCurrentCSeq;
    char* fOurSessionCookie; // used for optional RTSP-over-HTTP tunneling
    unsigned fBase64RemainderCount; // used for optional RTSP-over-HTTP tunneling (possible values: 0,1,2,3)

    Boolean fIsMulticast, fStreamAfterSETUP;
    unsigned char fTCPStreamIdCount; // used for (optional) RTP/TCP
    Boolean usesTCPTransport() const { return fTCPStreamIdCount > 0; }

    string fSessionName;
    string fTrackName;
    int fOurSessionId;

    //内部建立的循环
    UsageEnvironment* fEnv;

    MediaSessionMgr& fMediaSessionMgr;
};