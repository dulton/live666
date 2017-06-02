#include "MyRTSPClientConnection.h"
#include "MyServerMediaSession.hh"
#include "MediaSessionMgr.h"
#include "base64.hh"
#include <stdlib.h>

#include "RTSPCommon.hh"
#include "groupsockhelper.hh"
/*
--------------------------------------------------------------------底层

MediaSession
    能够发送RTP和RTCP的类
    提供SDP/媒体信息给RTSP
    管理Subsession,提供媒体功能
    有独立的密码(被否决,RTSP没有根据SDP的)

MediaSubSession
    设定SRC和DST
    增加一个目的地址

--------------------------------------------------------------------逻辑层(对外暴露的接口)
   
MediaSessionMgr
    管理MediaSession,全局唯一
    根据请求建立,删除一个MediaSession
    根据SessionName控制一个MediaSession
        Play Seek Pause Stop
    根据SessionName查询MediaSession信息

---------------------------------------------------------------------UI层(可以同时运行多个)

RTSP:
RTSPClientConnection
保存每个客户端的连接关系,自带线程
根据要求新建Dst和Src,如果可能,重用原有的并且增加一个目的地址

RTSPServer
    监听554端口,accept后新建一个RTSPClientConnection

RPC:
XML-RPC
    根据用户要求新建Dst和Src,如果可能,重用原有的并且增加一个目的地址

...


每个对fMediaSessionMgr的调用要判断错误返回
*/

#pragma warning( disable : 4996 )

static void lookForHeader(char const* headerName, char const* source, unsigned sourceLen, char* resultStr, unsigned resultMaxSize);

static Boolean parseAuthorizationHeader(char const* buf,
                                 char const*& username,
                                 char const*& realm,
                                 char const*& nonce, char const*& uri,
                                 char const*& response);

MyRTSPClientConnection::MyRTSPClientConnection(MediaSessionMgr& MediaMgr, int fOurSocket, struct sockaddr_in clientAddr)
:fMediaSessionMgr(MediaMgr),fClientInputSocket(fOurSocket), fClientOutputSocket(fOurSocket),
fIsActive(True), fRecursionCount(0), fOurSessionCookie(NULL) {
    incomingRequestHandler1();
    resetRequestBuffer();
    //这里创建loop线程,接收并处理数据
}

MyRTSPClientConnection::~MyRTSPClientConnection() {
    closeSocketsRTSP();
    delete this;
}

void MyRTSPClientConnection::incomingRequestHandler1() {
    struct sockaddr_in dummy; // 'from' address, meaningless in this case

    int bytesRead = readSocket(envir(), fClientInputSocket, &fRequestBuffer[fRequestBytesAlreadySeen], fRequestBufferBytesLeft, dummy);
    handleRequestBytes(bytesRead);
}

Boolean MyRTSPClientConnection
::authenticationOK(char const* cmdName, char const* urlSuffix, char const* fullRequestStr) {

    char const* username = NULL; char const* realm = NULL; char const* nonce = NULL;
    char const* uri = NULL; char const* response = NULL;
    Boolean success = False;
    string ourResponse;
    string sNonce = fMediaSessionMgr.Nonce(fSessionName);
    string sRealm = fMediaSessionMgr.Realm(fSessionName);

    do {
        // To authenticate, we first need to have a nonce set up
        // from a previous attempt:
        if (sNonce.size() <= 0) break;

        // Next, the request needs to contain an "Authorization:" header,
        // containing a username, (our) realm, (our) nonce, uri,
        // and response string:
        if (!parseAuthorizationHeader(fullRequestStr,
            username, realm, nonce, uri, response)
            || username == NULL
            || realm == NULL || (realm != sRealm)
            || nonce == NULL || (nonce != sNonce)
            || uri == NULL || response == NULL) {
                break;
        }

        success = fMediaSessionMgr.authentication(fSessionName, uri, username, realm, nonce, ourResponse);

    } while (0);

    delete[] (char*)realm; delete[] (char*)nonce;
    delete[] (char*)uri; delete[] (char*)response;
    delete[] (char*)username;
    if (success) return True;

    // If we get here, we failed to authenticate the user.
    // Send back a "401 Unauthorized" response, with a new random nonce:
    string sNewNonce = fMediaSessionMgr.NewNonce(fSessionName);
    snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
        "RTSP/1.0 401 Unauthorized\r\n"
        "CSeq: %s\r\n"
        "%s"
        "WWW-Authenticate: Digest realm=\"%s\", nonce=\"%s\"\r\n\r\n",
        fCurrentCSeq,
        dateHeader(),
        sRealm.c_str(), sNewNonce.c_str());
    return False;
}

void MyRTSPClientConnection::setRTSPResponse(char const* responseStr) {
    snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
        "RTSP/1.0 %s\r\n"
        "CSeq: %s\r\n"
        "%s\r\n",
        responseStr,
        fCurrentCSeq,
        dateHeader());
}

void MyRTSPClientConnection::setRTSPResponse(char const* responseStr, u_int32_t sessionId) {
    snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
        "RTSP/1.0 %s\r\n"
        "CSeq: %s\r\n"
        "%s"
        "Session: %08X\r\n\r\n",
        responseStr,
        fCurrentCSeq,
        dateHeader(),
        sessionId);
}

void MyRTSPClientConnection::setRTSPResponse(char const* responseStr, char const* contentStr) {
    if (contentStr == NULL) contentStr = "";
    size_t const contentLen = strlen(contentStr);

    snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
        "RTSP/1.0 %s\r\n"
        "CSeq: %s\r\n"
        "%s"
        "Content-Length: %d\r\n\r\n"
        "%s",
        responseStr,
        fCurrentCSeq,
        dateHeader(),
        contentLen,
        contentStr);
}

void MyRTSPClientConnection
::setRTSPResponse(char const* responseStr, u_int32_t sessionId, char const* contentStr) {
    if (contentStr == NULL) contentStr = "";
    size_t const contentLen = strlen(contentStr);

    snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
        "RTSP/1.0 %s\r\n"
        "CSeq: %s\r\n"
        "%s"
        "Session: %08X\r\n"
        "Content-Length: %d\r\n\r\n"
        "%s",
        responseStr,
        fCurrentCSeq,
        dateHeader(),
        sessionId,
        contentLen,
        contentStr);
}

void MyRTSPClientConnection::incomingRequestHandler(void* instance, int /*mask*/) {
    MyRTSPClientConnection* pThis = (MyRTSPClientConnection*)instance;

    struct sockaddr_in dummy; // 'from' address, meaningless in this case
    unsigned char* pBuf = pThis->fRequestBuffer+pThis->fRequestBytesAlreadySeen;
    int bytesRead = readSocket(pThis->envir(), pThis->fClientInputSocket, pBuf, pThis->fRequestBufferBytesLeft, dummy);
    pThis->handleRequestBytes(bytesRead);
}

void MyRTSPClientConnection
::changeClientInputSocket(int newSocketNum, unsigned char const* extraData, unsigned extraDataSize) {
    envir().taskScheduler().disableBackgroundHandling(fClientInputSocket);
    fClientInputSocket = newSocketNum;
    envir().taskScheduler().setBackgroundHandling(fClientInputSocket, SOCKET_READABLE|SOCKET_EXCEPTION,
        incomingRequestHandler, this);

    // Also write any extra data to our buffer, and handle it:
    if (extraDataSize > 0 && extraDataSize <= fRequestBufferBytesLeft/*sanity check; should always be true*/) {
        unsigned char* ptr = &fRequestBuffer[fRequestBytesAlreadySeen];
        for (unsigned i = 0; i < extraDataSize; ++i) {
            ptr[i] = extraData[i];
        }
        handleRequestBytes(extraDataSize);
    }
}


typedef enum StreamingMode {
    RTP_UDP,
    RTP_TCP,
    RAW_UDP
} StreamingMode;

static void parseTransportHeader(char const* buf,
                                 StreamingMode& streamingMode,
                                 string& streamingModeString,
                                 string& destinationAddressStr,
                                 u_int8_t& destinationTTL,
                                 portNumBits& clientRTPPortNum, // if UDP
                                 portNumBits& clientRTCPPortNum, // if UDP
                                 unsigned char& rtpChannelId, // if TCP
                                 unsigned char& rtcpChannelId // if TCP
                                 ) 
{
    // Initialize the result parameters to default values:
    streamingMode = RTP_UDP;
    destinationTTL = 255;
    clientRTPPortNum = 0;
    clientRTCPPortNum = 1;
    rtpChannelId = rtcpChannelId = 0xFF;

    portNumBits p1, p2;
    unsigned ttl, rtpCid, rtcpCid;

    // First, find "Transport:"
    while (1) {
     if (*buf == '\0') return; // not found
     if (*buf == '\r' && *(buf+1) == '\n' && *(buf+2) == '\r') return; // end of the headers => not found
     if (_strncasecmp(buf, "Transport:", 10) == 0) break;
     ++buf;
    }

    // Then, run through each of the fields, looking for ones we handle:
    char const* fields = buf + 10;
    while (*fields == ' ') ++fields;
    char* field = strDupSize(fields);
    while (sscanf(fields, "%[^;\r\n]", field) == 1) {
     if (strcmp(field, "RTP/AVP/TCP") == 0) {
         streamingMode = RTP_TCP;
     } else if (strcmp(field, "RAW/RAW/UDP") == 0 ||
         strcmp(field, "MP2T/H2221/UDP") == 0) {
             streamingMode = RAW_UDP;
             streamingModeString = field;
     } else if (_strncasecmp(field, "destination=", 12) == 0) {
         destinationAddressStr = field+12;
     } else if (sscanf(field, "ttl%u", &ttl) == 1) {
         destinationTTL = (u_int8_t)ttl;
     } else if (sscanf(field, "client_port=%hu-%hu", &p1, &p2) == 2) {
         clientRTPPortNum = p1;
         clientRTCPPortNum = streamingMode == RAW_UDP ? 0 : p2; // ignore the second port number if the client asked for raw UDP
     } else if (sscanf(field, "client_port=%hu", &p1) == 1) {
         clientRTPPortNum = p1;
         clientRTCPPortNum = streamingMode == RAW_UDP ? 0 : p1 + 1;
     } else if (sscanf(field, "interleaved=%u-%u", &rtpCid, &rtcpCid) == 2) {
         rtpChannelId = (unsigned char)rtpCid;
         rtcpChannelId = (unsigned char)rtcpCid;
     }

     fields += strlen(field);
     while (*fields == ';' || *fields == ' ' || *fields == '\t') ++fields; // skip over separating ';' chars or whitespace
     if (*fields == '\0' || *fields == '\r' || *fields == '\n') break;
    }
    delete[] field;
}

static Boolean parsePlayNowHeader(char const* buf) {
    // Find "x-playNow:" header, if present
    while (1) {
        if (*buf == '\0') return False; // not found
        if (_strncasecmp(buf, "x-playNow:", 10) == 0) break;
        ++buf;
    }

    return True;
}

void MyRTSPClientConnection
::handleCmd_SETUP(char const* urlPreSuffix, char const* urlSuffix, char const* fullRequestStr) 
{
    //原有的代码处理了没有后缀的情况,太啰嗦,直接删掉了
    char const* streamName = urlPreSuffix; // in the normal case
    char const* trackId = urlSuffix; // in the normal case

    //功能占位符:这里SETUP命令根据trackId查找subsession,如果找不到或者trackId为空返回各自错误
    if (fMediaSessionMgr.requestMediaSession(streamName) == NULL){
        handleCmd_notFound();
    }
    fSessionName = streamName;
    fTrackName = trackId;

    //如果是同一个用户第二次打开相同的trackId则从头播放???这段逻辑不明


    // Look for a "Transport:" header in the request string, to extract client parameters:
    StreamingMode streamingMode;
    string streamingModeString; // set when RAW_UDP streaming is specified
    string clientsDestinationAddressStr;
    u_int8_t clientsDestinationTTL;
    portNumBits clientRTPPortNum, clientRTCPPortNum;
    unsigned char rtpChannelId, rtcpChannelId;
    parseTransportHeader(fullRequestStr, streamingMode, streamingModeString,
        clientsDestinationAddressStr, clientsDestinationTTL,
        clientRTPPortNum, clientRTCPPortNum,
        rtpChannelId, rtcpChannelId);
    if ((streamingMode == RTP_TCP && rtpChannelId == 0xFF) ||
        (streamingMode != RTP_TCP && fClientOutputSocket != fClientInputSocket)) {
            // An anomolous situation, caused by a buggy client.  Either:
            //     1/ TCP streaming was requested, but with no "interleaving=" fields.  (QuickTime Player sometimes does this.), or
            //     2/ TCP streaming was not requested, but we're doing RTSP-over-HTTP tunneling (which implies TCP streaming).
            // In either case, we assume TCP streaming, and set the RTP and RTCP channel ids to proper values:
            streamingMode = RTP_TCP;
            rtpChannelId = fTCPStreamIdCount; rtcpChannelId = fTCPStreamIdCount+1;
    }
    if (streamingMode == RTP_TCP) fTCPStreamIdCount += 2;

    Port clientRTPPort(clientRTPPortNum);
    Port clientRTCPPort(clientRTCPPortNum);

    // Next, check whether a "Range:" or "x-playNow:" header is present in the request.
    // This isn't legal, but some clients do this to combine "SETUP" and "PLAY":
    double rangeStart = 0.0, rangeEnd = 0.0;
    char* absStart = NULL; char* absEnd = NULL;
    Boolean startTimeIsNow;
    if (parseRangeHeader(fullRequestStr, rangeStart, rangeEnd, absStart, absEnd, startTimeIsNow)) {
        delete[] absStart; delete[] absEnd;
        fStreamAfterSETUP = True;
    } else if (parsePlayNowHeader(fullRequestStr)) {
        fStreamAfterSETUP = True;
    } else {
        fStreamAfterSETUP = False;
    }

    netAddressBits destinationAddress = 0;
    u_int8_t destinationTTL = 255;
#ifdef RTSP_ALLOW_CLIENT_DESTINATION_SETTING
    if (clientsDestinationAddressStr != NULL) {
        // Use the client-provided "destination" address.
        // Note: This potentially allows the server to be used in denial-of-service
        // attacks, so don't enable this code unless you're sure that clients are
        // trusted.
        destinationAddress = our_inet_addr(clientsDestinationAddressStr);
    }
    // Also use the client-provided TTL.
    destinationTTL = clientsDestinationTTL;
#endif
    Port serverRTPPort(0);
    Port serverRTCPPort(0);

    // Make sure that we transmit on the same interface that's used by the client (in case we're a multi-homed server):
    struct sockaddr_in sourceAddr; SOCKLEN_T namelen = sizeof sourceAddr;
    getsockname(fClientInputSocket, (struct sockaddr*)&sourceAddr, &namelen);
    netAddressBits origSendingInterfaceAddr = SendingInterfaceAddr;
    netAddressBits origReceivingInterfaceAddr = ReceivingInterfaceAddr;
    // NOTE: The following might not work properly, so we ifdef it out for now:
#ifdef HACK_FOR_MULTIHOMED_SERVERS
    ReceivingInterfaceAddr = SendingInterfaceAddr = sourceAddr.sin_addr.s_addr;
#endif
    
    //设置发送参数,获得一些参数
    fMediaSessionMgr.getStreamParameters(fSessionName, fOurSessionId, 
        fClientAddr.sin_addr.s_addr,
        clientRTPPort, 
        clientRTCPPort,
        fClientOutputSocket, 
        rtpChannelId, 
        rtcpChannelId,
        destinationAddress, 
        destinationTTL, 
        fIsMulticast,
        serverRTPPort, 
        serverRTCPPort);

    SendingInterfaceAddr = origSendingInterfaceAddr;
    ReceivingInterfaceAddr = origReceivingInterfaceAddr;

    AddressString destAddrStr(destinationAddress);
    AddressString sourceAddrStr(sourceAddr);
    //...去掉timeout参数,奇怪的东西
    do{
    if (fIsMulticast) {
        switch (streamingMode) {
            case RTP_UDP: {
                snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
                    "RTSP/1.0 200 OK\r\n"
                    "CSeq: %s\r\n"
                    "%s"
                    "Transport: RTP/AVP;multicast;destination=%s;source=%s;port=%d-%d;ttl=%d\r\n"
                    "Session: %08X\r\n\r\n",
                    fCurrentCSeq,
                    dateHeader(),
                    destAddrStr.val(), sourceAddrStr.val(), ntohs(serverRTPPort.num()), ntohs(serverRTCPPort.num()), destinationTTL,
                    fOurSessionId);
                break;
                          }
            case RTP_TCP: {
                // multicast streams can't be sent via TCP
                handleCmd_unsupportedTransport();
                break;
                          }
            case RAW_UDP: {
                snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
                    "RTSP/1.0 200 OK\r\n"
                    "CSeq: %s\r\n"
                    "%s"
                    "Transport: %s;multicast;destination=%s;source=%s;port=%d;ttl=%d\r\n"
                    "Session: %08X\r\n\r\n",
                    fCurrentCSeq,
                    dateHeader(),
                    streamingModeString, destAddrStr.val(), sourceAddrStr.val(), ntohs(serverRTPPort.num()), destinationTTL,
                    fOurSessionId);
                break;
            }
        }
    } else 
    {
        switch (streamingMode) 
        {
        case RTP_UDP: {
            snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "%s"
                "Transport: RTP/AVP;unicast;destination=%s;source=%s;client_port=%d-%d;server_port=%d-%d\r\n"
                "Session: %08X\r\n\r\n",
                fCurrentCSeq,
                dateHeader(),
                destAddrStr.val(), sourceAddrStr.val(), ntohs(clientRTPPort.num()), ntohs(clientRTCPPort.num()), ntohs(serverRTPPort.num()), ntohs(serverRTCPPort.num()),
                fOurSessionId);
            break;
                      }
        case RTP_TCP: {
            snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "%s"
                "Transport: RTP/AVP/TCP;unicast;destination=%s;source=%s;interleaved=%d-%d\r\n"
                "Session: %08X\r\n\r\n",
                fCurrentCSeq,
                dateHeader(),
                destAddrStr.val(), sourceAddrStr.val(), rtpChannelId, rtcpChannelId,
                fOurSessionId);
                      }
        case RAW_UDP: {
            snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "%s"
                "Transport: %s;unicast;destination=%s;source=%s;client_port=%d;server_port=%d\r\n"
                "Session: %08X\r\n\r\n",
                fCurrentCSeq,
                dateHeader(),
                streamingModeString, destAddrStr.val(), sourceAddrStr.val(), ntohs(clientRTPPort.num()), ntohs(serverRTPPort.num()),
                fOurSessionId);
            break;
            }
        }
    }
} while (0);

}

void MyRTSPClientConnection::handleCmd_withinSession(char const* cmdName,
                          char const* urlPreSuffix, char const* urlSuffix,
                          char const* fullRequestStr) {
    // This will either be:
    // - a non-aggregated operation, if "urlPreSuffix" is the session (stream)
    //   name and "urlSuffix" is the subsession (track) name, or
    // - an aggregated operation, if "urlSuffix" is the session (stream) name,
    //   or "urlPreSuffix" is the session (stream) name, and "urlSuffix" is empty,
    //   or "urlPreSuffix" and "urlSuffix" are both nonempty, but when concatenated, (with "/") form the session (stream) name.
    // Begin by figuring out which of these it is:
    char const* streamName = urlPreSuffix; // in the normal case
    char const* trackId = urlSuffix; // in the normal case

    if (fMediaSessionMgr.requestMediaSession(streamName) == NULL){
      handleCmd_notFound();
    }

    if (strcmp(cmdName, "TEARDOWN") == 0) {
      handleCmd_TEARDOWN();
    } else if (strcmp(cmdName, "PLAY") == 0) {
      handleCmd_PLAY(fullRequestStr);
    } else if (strcmp(cmdName, "PAUSE") == 0) {
      handleCmd_PAUSE();
    } else if (strcmp(cmdName, "GET_PARAMETER") == 0) {
      handleCmd_GET_PARAMETER(fullRequestStr);
    } else if (strcmp(cmdName, "SET_PARAMETER") == 0) {
      handleCmd_SET_PARAMETER(fullRequestStr);
    }
}

//为什么要在fOurRTSPServer里管理TCP socket??
void MyRTSPClientConnection::handleCmd_TEARDOWN() 
{
    fMediaSessionMgr.stopStream(fSessionName);

    setRTSPResponse("200 OK");
}

void MyRTSPClientConnection
::handleCmd_PLAY(char const* fullRequestStr) {

    // Parse the client's "Scale:" header, if any:
    float scale;
    Boolean sawScaleHeader = parseScaleHeader(fullRequestStr, scale);

    fMediaSessionMgr.setStreamScale(fSessionName, scale);

    char buf[100];
    char* scaleHeader;
    if (!sawScaleHeader) {
     buf[0] = '\0'; // Because we didn't see a Scale: header, don't send one back
    } else {
     sprintf(buf, "Scale: %f\r\n", scale);
    }
    scaleHeader = strDup(buf);

    // Parse the client's "Range:" header, if any:
    float duration = 0.0;
    double rangeStart = 0.0, rangeEnd = 0.0;
    char* absStart = NULL; char* absEnd = NULL;
    Boolean startTimeIsNow;
    Boolean sawRangeHeader
     = parseRangeHeader(fullRequestStr, rangeStart, rangeEnd, absStart, absEnd, startTimeIsNow);

    if (sawRangeHeader && absStart == NULL/*not seeking by 'absolute' time*/) {
     // Use this information, plus the stream's duration (if known), to create our own "Range:" header, for the response:
     duration = fMediaSessionMgr.duration();
     if (duration < 0.0) {
         // We're an aggregate PLAY, but the subsessions have different durations.
         // Use the largest of these durations in our header
         duration = -duration;
     }

     // Make sure that "rangeStart" and "rangeEnd" (from the client's "Range:" header)
     // have sane values, before we send back our own "Range:" header in our response:
     if (rangeStart < 0.0) rangeStart = 0.0;
     else if (rangeStart > duration) rangeStart = duration;
     if (rangeEnd < 0.0) rangeEnd = 0.0;
     else if (rangeEnd > duration) rangeEnd = duration;
     if ((scale > 0.0 && rangeStart > rangeEnd && rangeEnd > 0.0) ||
         (scale < 0.0 && rangeStart < rangeEnd)) {
             // "rangeStart" and "rangeEnd" were the wrong way around; swap them:
             double tmp = rangeStart;
             rangeStart = rangeEnd;
             rangeEnd = tmp;
     }
    }

    // Create a "RTP-Info:" line.  It will get filled in from each subsession's state:
    char const* rtpInfoFmt =
     "%s" // "RTP-Info:", plus any preceding rtpInfo items
     "%s" // comma separator, if needed
     "url=%s/%s"
     ";seq=%d"
     ";rtptime=%u"
     ;
    size_t rtpInfoFmtSize = strlen(rtpInfoFmt);
    char* rtpInfo = strDup("RTP-Info: ");
    unsigned numRTPInfoItems = 0;

    // Do any required seeking/scaling on each subsession, before starting streaming.
    // (However, we don't do this if the "PLAY" request was for just a single subsession
    // of a multiple-subsession stream; for such streams, seeking/scaling can be done
    // only with an aggregate "PLAY".)
    if (sawScaleHeader) {
        fMediaSessionMgr.setStreamScale(fSessionName, scale);
    }
    if (absStart != NULL) {
        // Special case handling for seeking by 'absolute' time:

        fMediaSessionMgr.seekStream(fSessionName, absStart, absEnd);
    } else {
        // Seeking by relative (NPT) time:

        u_int64_t numBytes;
        if (!sawRangeHeader || startTimeIsNow) {
            // We're resuming streaming without seeking, so we just do a 'null' seek
            // (to get our NPT, and to specify when to end streaming):
            fMediaSessionMgr.nullSeekStream(fSessionName,
                rangeEnd, numBytes);
        } else {
            // We do a real 'seek':
            double streamDuration = 0.0; // by default; means: stream until the end of the media
            if (rangeEnd > 0.0 && (rangeEnd+0.001) < duration) {
                // the 0.001 is because we limited the values to 3 decimal places
                // We want the stream to end early.  Set the duration we want:
                streamDuration = rangeEnd - rangeStart;
                if (streamDuration < 0.0) streamDuration = -streamDuration;
                // should happen only if scale < 0.0
            }
            fMediaSessionMgr.seekStream(fSessionName, rangeStart, streamDuration, numBytes);
        }
    }

    // Create the "Range:" header that we'll send back in our response.
    // (Note that we do this after seeking, in case the seeking operation changed the range start time.)
    if (absStart != NULL) {
     // We're seeking by 'absolute' time:
     if (absEnd == NULL) {
         sprintf(buf, "Range: clock=%s-\r\n", absStart);
     } else {
         sprintf(buf, "Range: clock=%s-%s\r\n", absStart, absEnd);
     }
     delete[] absStart; delete[] absEnd;
    } else {
     // We're seeking by relative (NPT) time:
     if (!sawRangeHeader || startTimeIsNow) {
         // We didn't seek, so in our response, begin the range with the current NPT (normal play time):
         float curNPT = 0.0;
         float npt = fMediaSessionMgr.getCurrentNPT();
         if (npt > curNPT) curNPT = npt;
         rangeStart = curNPT;
     }

     if (rangeEnd == 0.0 && scale >= 0.0) {
         sprintf(buf, "Range: npt=%.3f-\r\n", rangeStart);
     } else {
         sprintf(buf, "Range: npt=%.3f-%.3f\r\n", rangeStart, rangeEnd);
     }
    }
    char* rangeHeader = strDup(buf);

    // Now, start streaming:
    fMediaSessionMgr.playStream(fSessionName);

    string sStreamInfo;
    fMediaSessionMgr.getRtpInfo(sStreamInfo);
    
    // Fill in the response:
    snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
     "RTSP/1.0 200 OK\r\n"
     "CSeq: %s\r\n"
     "%s"
     "%s"
     "%s"
     "Session: %08X\r\n"
     "%s\r\n",
     fCurrentCSeq,
     dateHeader(),
     scaleHeader,
     rangeHeader,
     fOurSessionId,
     rtpInfo);
}

void MyRTSPClientConnection::handleCmd_PAUSE() 
{
    fMediaSessionMgr.pauseStream(fSessionName);

    setRTSPResponse("200 OK", fOurSessionId);
}

void MyRTSPClientConnection::handleCmd_GET_PARAMETER(char const* /*fullRequestStr*/) {
// By default, we implement "GET_PARAMETER" just as a 'keep alive', and send back a dummy response.
// (If you want to handle "GET_PARAMETER" properly, you can do so by defining a subclass of "RTSPServer"
// and "RTSPServer::RTSPClientSession", and then reimplement this virtual function in your subclass.)
setRTSPResponse("200 OK", fOurSessionId, LIVEMEDIA_LIBRARY_VERSION_STRING);
}

void MyRTSPClientConnection
::handleCmd_SET_PARAMETER(char const* /*fullRequestStr*/) {
    // By default, we implement "SET_PARAMETER" just as a 'keep alive', and send back an empty response.
    // (If you want to handle "SET_PARAMETER" properly, you can do so by defining a subclass of "RTSPServer"
    // and "RTSPServer::RTSPClientSession", and then reimplement this virtual function in your subclass.)
    setRTSPResponse("200 OK", fOurSessionId);
}

static void lookForHeader(char const* headerName, char const* source, unsigned sourceLen, char* resultStr, unsigned resultMaxSize) {
    resultStr[0] = '\0';  // by default, return an empty string
    size_t headerNameLen = strlen(headerName);
    for (size_t i = 0; i < (size_t)(sourceLen-headerNameLen); ++i) {
        if (strncmp(&source[i], headerName, headerNameLen) == 0 && source[i+headerNameLen] == ':') {
            // We found the header.  Skip over any whitespace, then copy the rest of the line to "resultStr":
            for (i += headerNameLen+1; i < (size_t)sourceLen && (source[i] == ' ' || source[i] == '\t'); ++i) {}
            for (size_t j = i; j < sourceLen; ++j) {
                if (source[j] == '\r' || source[j] == '\n') {
                    // We've found the end of the line.  Copy it to the result (if it will fit):
                    if (j-i+1 > resultMaxSize) break;
                    char const* resultSource = &source[i];
                    char const* resultSourceEnd = &source[j];
                    while (resultSource < resultSourceEnd) *resultStr++ = *resultSource++;
                    *resultStr = '\0';
                    break;
                }
            }
        }
    }
}

void MyRTSPClientConnection::handleAlternativeRequestByte1(u_int8_t requestByte) {
    if (requestByte == 0xFF) {
        // Hack: The new handler of the input TCP socket encountered an error reading it.  Indicate this:
        handleRequestBytes(-1);
    } else if (requestByte == 0xFE) {
        // Another hack: The new handler of the input TCP socket no longer needs it, so take back control of it:
        envir().taskScheduler().setBackgroundHandling(fClientInputSocket, SOCKET_READABLE|SOCKET_EXCEPTION,
            (TaskScheduler::BackgroundHandlerProc*)&incomingRequestHandler, this);
    } else {
        // Normal case: Add this character to our buffer; then try to handle the data that we have buffered so far:
        if (fRequestBufferBytesLeft == 0 || fRequestBytesAlreadySeen >= RTSP_BUFFER_SIZE) return;
        fRequestBuffer[fRequestBytesAlreadySeen] = requestByte;
        handleRequestBytes(1);
    }
}

void MyRTSPClientConnection::handleRequestBytes(int newBytesRead) {
    int numBytesRemaining = 0;
    ++fRecursionCount;

    do {
        if (newBytesRead < 0 || (unsigned)newBytesRead >= fRequestBufferBytesLeft) {
            // Either the client socket has died, or the request was too big for us.
            // Terminate this connection:
#ifdef DEBUG
            fprintf(stderr, "RTSPClientConnection[%p]::handleRequestBytes() read %d new bytes (of %d); terminating connection!\n", this, newBytesRead, fRequestBufferBytesLeft);
#endif
            fIsActive = False;
            break;
        }

        Boolean endOfMsg = False;
        unsigned char* ptr = &fRequestBuffer[fRequestBytesAlreadySeen];
#ifdef DEBUG
        ptr[newBytesRead] = '\0';
        fprintf(stderr, "RTSPClientConnection[%p]::handleRequestBytes() %s %d new bytes:%s\n",
            this, numBytesRemaining > 0 ? "processing" : "read", newBytesRead, ptr);
#endif

        if (fClientOutputSocket != fClientInputSocket && numBytesRemaining == 0) {
            // We're doing RTSP-over-HTTP tunneling, and input commands are assumed to have been Base64-encoded.
            // We therefore Base64-decode as much of this new data as we can (i.e., up to a multiple of 4 bytes).

            // But first, we remove any whitespace that may be in the input data:
            unsigned toIndex = 0;
            for (int fromIndex = 0; fromIndex < newBytesRead; ++fromIndex) {
                char c = ptr[fromIndex];
                if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n')) { // not 'whitespace': space,tab,CR,NL
                    ptr[toIndex++] = c;
                }
            }
            newBytesRead = toIndex;

            unsigned numBytesToDecode = fBase64RemainderCount + newBytesRead;
            unsigned newBase64RemainderCount = numBytesToDecode%4;
            numBytesToDecode -= newBase64RemainderCount;
            if (numBytesToDecode > 0) {
                ptr[newBytesRead] = '\0';
                unsigned decodedSize;
                unsigned char* decodedBytes = base64Decode((char const*)(ptr-fBase64RemainderCount), numBytesToDecode, decodedSize);
#ifdef DEBUG
                fprintf(stderr, "Base64-decoded %d input bytes into %d new bytes:", numBytesToDecode, decodedSize);
                for (unsigned k = 0; k < decodedSize; ++k) fprintf(stderr, "%c", decodedBytes[k]);
                fprintf(stderr, "\n");
#endif

                // Copy the new decoded bytes in place of the old ones (we can do this because there are fewer decoded bytes than original):
                unsigned char* to = ptr-fBase64RemainderCount;
                for (unsigned i = 0; i < decodedSize; ++i) *to++ = decodedBytes[i];

                // Then copy any remaining (undecoded) bytes to the end:
                for (unsigned j = 0; j < newBase64RemainderCount; ++j) *to++ = (ptr-fBase64RemainderCount+numBytesToDecode)[j];

                newBytesRead = decodedSize - fBase64RemainderCount + newBase64RemainderCount;
                // adjust to allow for the size of the new decoded data (+ remainder)
                delete[] decodedBytes;
            }
            fBase64RemainderCount = newBase64RemainderCount;
        }

        unsigned char *tmpPtr = fLastCRLF + 2;
        if (fBase64RemainderCount == 0) { // no more Base-64 bytes remain to be read/decoded
            // Look for the end of the message: <CR><LF><CR><LF>
            if (tmpPtr < fRequestBuffer) tmpPtr = fRequestBuffer;
            while (tmpPtr < &ptr[newBytesRead-1]) {
                if (*tmpPtr == '\r' && *(tmpPtr+1) == '\n') {
                    if (tmpPtr - fLastCRLF == 2) { // This is it:
                        endOfMsg = True;
                        break;
                    }
                    fLastCRLF = tmpPtr;
                }
                ++tmpPtr;
            }
        }

        fRequestBufferBytesLeft -= newBytesRead;
        fRequestBytesAlreadySeen += newBytesRead;

        if (!endOfMsg) break; // subsequent reads will be needed to complete the request

        // Parse the request string into command name and 'CSeq', then handle the command:
        fRequestBuffer[fRequestBytesAlreadySeen] = '\0';
        char cmdName[RTSP_PARAM_STRING_MAX];
        char urlPreSuffix[RTSP_PARAM_STRING_MAX];
        char urlSuffix[RTSP_PARAM_STRING_MAX];
        char cseq[RTSP_PARAM_STRING_MAX];
        char sessionIdStr[RTSP_PARAM_STRING_MAX];
        unsigned contentLength = 0;
        fLastCRLF[2] = '\0'; // temporarily, for parsing
        Boolean parseSucceeded = parseRTSPRequestString((char*)fRequestBuffer, fLastCRLF+2 - fRequestBuffer,
            cmdName, sizeof cmdName,
            urlPreSuffix, sizeof urlPreSuffix,
            urlSuffix, sizeof urlSuffix,
            cseq, sizeof cseq,
            sessionIdStr, sizeof sessionIdStr,
            contentLength);
        fLastCRLF[2] = '\r'; // restore its value
        Boolean playAfterSetup = False;
        if (parseSucceeded) {
#ifdef DEBUG
            fprintf(stderr, "parseRTSPRequestString() succeeded, returning cmdName \"%s\", urlPreSuffix \"%s\", urlSuffix \"%s\", CSeq \"%s\", Content-Length %u, with %ld bytes following the message.\n", cmdName, urlPreSuffix, urlSuffix, cseq, contentLength, ptr + newBytesRead - (tmpPtr + 2));
#endif
            // If there was a "Content-Length:" header, then make sure we've received all of the data that it specified:
            if (ptr + newBytesRead < tmpPtr + 2 + contentLength) break; // we still need more data; subsequent reads will give it to us 

            // If the request included a "Session:" id, and it refers to a client session that's
            // current ongoing, then use this command to indicate 'liveness' on that client session:

            //这里去掉了RTSP请求中指定Session值的处理。为什么要指定?给谁处理不是处理?

            // We now have a complete RTSP request.
            // Handle the specified command (beginning with commands that are session-independent):
            fCurrentCSeq = cseq;
            if (strcmp(cmdName, "OPTIONS") == 0) {
                // If the "OPTIONS" command included a "Session:" id for a session that doesn't exist,
                // then treat this as an error:
                handleCmd_OPTIONS();
            } else if (urlPreSuffix[0] == '\0' && urlSuffix[0] == '*' && urlSuffix[1] == '\0') {
                // The special "*" URL means: an operation on the entire server.  This works only for GET_PARAMETER and SET_PARAMETER:
                if (strcmp(cmdName, "GET_PARAMETER") == 0) {
                    handleCmd_GET_PARAMETER((char const*)fRequestBuffer);
                } else if (strcmp(cmdName, "SET_PARAMETER") == 0) {
                    handleCmd_SET_PARAMETER((char const*)fRequestBuffer);
                } else {
                    handleCmd_notSupported();
                }
            } else if (strcmp(cmdName, "DESCRIBE") == 0) {
                handleCmd_DESCRIBE(urlPreSuffix, urlSuffix, (char const*)fRequestBuffer);
            } else if (strcmp(cmdName, "SETUP") == 0) {

                char urlTotalSuffix[2*RTSP_PARAM_STRING_MAX];
                // enough space for urlPreSuffix/urlSuffix'\0'
                urlTotalSuffix[0] = '\0';
                if (urlPreSuffix[0] != '\0') {
                    strcat(urlTotalSuffix, urlPreSuffix);
                    strcat(urlTotalSuffix, "/");
                }

                strcat(urlTotalSuffix, urlSuffix);

                if (authenticationOK("SETUP", urlTotalSuffix, (char const*)fRequestBuffer)) {
                    handleCmd_SETUP(urlPreSuffix, urlSuffix, (char const*)fRequestBuffer);
                    playAfterSetup = fStreamAfterSETUP;
                }

            } else if (strcmp(cmdName, "TEARDOWN") == 0
                || strcmp(cmdName, "PLAY") == 0
                || strcmp(cmdName, "PAUSE") == 0
                || strcmp(cmdName, "GET_PARAMETER") == 0
                || strcmp(cmdName, "SET_PARAMETER") == 0) {
                handleCmd_withinSession(cmdName, urlPreSuffix, urlSuffix, (char const*)fRequestBuffer);
            }else {
                // The command is one that we don't handle:
                handleCmd_notSupported();
            }
        } else {
#ifdef DEBUG
            fprintf(stderr, "parseRTSPRequestString() failed; checking now for HTTP commands (for RTSP-over-HTTP tunneling)...\n");
#endif
            // The request was not (valid) RTSP, but check for a special case: HTTP commands (for setting up RTSP-over-HTTP tunneling):
            char sessionCookie[RTSP_PARAM_STRING_MAX];
            char acceptStr[RTSP_PARAM_STRING_MAX];
            *fLastCRLF = '\0'; // temporarily, for parsing
            parseSucceeded = parseHTTPRequestString(cmdName, sizeof cmdName,
                urlSuffix, sizeof urlPreSuffix,
                sessionCookie, sizeof sessionCookie,
                acceptStr, sizeof acceptStr);
            *fLastCRLF = '\r';
            if (parseSucceeded) {
#ifdef DEBUG
                fprintf(stderr, "parseHTTPRequestString() succeeded, returning cmdName \"%s\", urlSuffix \"%s\", sessionCookie \"%s\", acceptStr \"%s\"\n", cmdName, urlSuffix, sessionCookie, acceptStr);
#endif
                // Check that the HTTP command is valid for RTSP-over-HTTP tunneling: There must be a 'session cookie'.
                Boolean isValidHTTPCmd = True;
                if (strcmp(cmdName, "OPTIONS") == 0) {
                    handleHTTPCmd_OPTIONS();
                } else if (sessionCookie[0] == '\0') {
                    // There was no "x-sessioncookie:" header.  If there was an "Accept: application/x-rtsp-tunnelled" header,
                    // then this is a bad tunneling request.  Otherwise, assume that it's an attempt to access the stream via HTTP.
                    if (strcmp(acceptStr, "application/x-rtsp-tunnelled") == 0) {
                        isValidHTTPCmd = False;
                    } else {
                        handleHTTPCmd_StreamingGET(urlSuffix, (char const*)fRequestBuffer);
                    }
                } else if (strcmp(cmdName, "GET") == 0) {
                    handleHTTPCmd_TunnelingGET(sessionCookie);
                } else if (strcmp(cmdName, "POST") == 0) {
                    // We might have received additional data following the HTTP "POST" command - i.e., the first Base64-encoded RTSP command.
                    // Check for this, and handle it if it exists:
                    unsigned char const* extraData = fLastCRLF+4;
                    unsigned extraDataSize = &fRequestBuffer[fRequestBytesAlreadySeen] - extraData;
                    if (handleHTTPCmd_TunnelingPOST(sessionCookie, extraData, extraDataSize)) {
                        // We don't respond to the "POST" command, and we go away:
                        fIsActive = False;
                        break;
                    }
                } else {
                    isValidHTTPCmd = False;
                }
                if (!isValidHTTPCmd) {
                    handleHTTPCmd_notSupported();
                }
            } else {
#ifdef DEBUG
                fprintf(stderr, "parseHTTPRequestString() failed!\n");
#endif
                handleCmd_bad();
            }
        }

#ifdef DEBUG
        fprintf(stderr, "sending response: %s", fResponseBuffer);
#endif
        send(fClientOutputSocket, (char const*)fResponseBuffer, strlen((char*)fResponseBuffer), 0);

        if (playAfterSetup) {
            // The client has asked for streaming to commence now, rather than after a
            // subsequent "PLAY" command.  So, simulate the effect of a "PLAY" command:
            handleCmd_withinSession("PLAY", urlPreSuffix, urlSuffix, (char const*)fRequestBuffer);
        }

        // Check whether there are extra bytes remaining in the buffer, after the end of the request (a rare case).
        // If so, move them to the front of our buffer, and keep processing it, because it might be a following, pipelined request.
        unsigned requestSize = (fLastCRLF+4-fRequestBuffer) + contentLength;
        numBytesRemaining = fRequestBytesAlreadySeen - requestSize;
        resetRequestBuffer(); // to prepare for any subsequent request

        if (numBytesRemaining > 0) {
            memmove(fRequestBuffer, &fRequestBuffer[requestSize], numBytesRemaining);
            newBytesRead = numBytesRemaining;
        }
    } while (numBytesRemaining > 0);

    --fRecursionCount;
    if (!fIsActive) {
        if (fRecursionCount > 0) closeSockets(); else delete this;
        // Note: The "fRecursionCount" test is for a pathological situation where we reenter the event loop and get called recursively
        // while handling a command (e.g., while handling a "DESCRIBE", to get a SDP description).
        // In such a case we don't want to actually delete ourself until we leave the outermost call.
    }
}

void MyRTSPClientConnection::handleCmd_OPTIONS() {
    snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
        "RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sPublic: %s\r\n\r\n",
        fCurrentCSeq, dateHeader(), "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER");
}

void MyRTSPClientConnection
::handleCmd_DESCRIBE(char const* urlPreSuffix, char const* urlSuffix, char const* fullRequestStr) {
    char* sdpDescription = NULL;
    char* rtspURL = NULL;
    do {
        char urlTotalSuffix[2*RTSP_PARAM_STRING_MAX];
        // enough space for urlPreSuffix/urlSuffix'\0'
        urlTotalSuffix[0] = '\0';
        if (urlPreSuffix[0] != '\0') {
            strcat(urlTotalSuffix, urlPreSuffix);
            strcat(urlTotalSuffix, "/");
        }
        strcat(urlTotalSuffix, urlSuffix);

        if (!authenticationOK("DESCRIBE", urlTotalSuffix, fullRequestStr)) break;

        // We should really check that the request contains an "Accept:" #####
        // for "application/sdp", because that's what we're sending back #####

        // Begin by looking up the "ServerMediaSession" object for the specified "urlTotalSuffix":
        Boolean bExist = fMediaSessionMgr.isMediaSessionExist(urlTotalSuffix);
        if (!bExist) {
            handleCmd_notFound();
            break;
        }

        // Then, assemble a SDP description for this session:
        string Sdp;
        if (!fMediaSessionMgr.generateSDPDescription(Sdp)){
            // This usually means that a file name that was specified for a
            // "ServerMediaSubsession" does not exist.
            setRTSPResponse("404 File Not Found, Or In Incorrect Format");
            break;
        }

        // Also, generate our RTSP URL, for the "Content-Base:" header
        // (which is necessary to ensure that the correct URL gets used in subsequent "SETUP" requests).
        rtspURL = "rtsp://xxxxxxxxxx";//这里为什么还要给出一遍RTSP地址?

        snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
            "RTSP/1.0 200 OK\r\nCSeq: %s\r\n"
            "%s"
            "Content-Base: %s/\r\n"
            "Content-Type: application/sdp\r\n"
            "Content-Length: %d\r\n\r\n"
            "%s",
            fCurrentCSeq,
            dateHeader(),
            rtspURL,
            Sdp.size(),
            Sdp.c_str());
    } while (0);

    delete[] rtspURL;
}

void MyRTSPClientConnection::closeSockets() {
/*    if (fOurSocket>= 0) ::closeSocket(fOurSocket);

    fOurSocket = -1;*/
}

static Boolean parseAuthorizationHeader(char const* buf,
                                        char const*& username,
                                        char const*& realm,
                                        char const*& nonce, char const*& uri,
                                        char const*& response) 
{
    // Initialize the result parameters to default values:
    username = realm = nonce = uri = response = NULL;

    // First, find "Authorization:"
    while (1) {
        if (*buf == '\0') return False; // not found
        if (_strncasecmp(buf, "Authorization: Digest ", 22) == 0) break;
        ++buf;
    }

    // Then, run through each of the fields, looking for ones we handle:
    char const* fields = buf + 22;
    while (*fields == ' ') ++fields;
    char* parameter = strDupSize(fields);
    char* value = strDupSize(fields);
    while (1) {
        value[0] = '\0';
        if (sscanf(fields, "%[^=]=\"%[^\"]\"", parameter, value) != 2 &&
            sscanf(fields, "%[^=]=\"\"", parameter) != 1) {
                break;
        }
        if (strcmp(parameter, "username") == 0) {
            username = strDup(value);
        } else if (strcmp(parameter, "realm") == 0) {
            realm = strDup(value);
        } else if (strcmp(parameter, "nonce") == 0) {
            nonce = strDup(value);
        } else if (strcmp(parameter, "uri") == 0) {
            uri = strDup(value);
        } else if (strcmp(parameter, "response") == 0) {
            response = strDup(value);
        }

        fields += strlen(parameter) + 2 /*="*/ + strlen(value) + 1 /*"*/;
        while (*fields == ',' || *fields == ' ') ++fields;
        // skip over any separating ',' and ' ' chars
        if (*fields == '\0' || *fields == '\r' || *fields == '\n') break;
    }
    delete[] parameter; delete[] value;
    return True;
}