/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2017 Live Networks, Inc.  All rights reserved.
// A RTSP server
// Implementation

#include "RTSPServer.hh"
#include "RTSPCommon.hh"
#include "RTSPRegisterSender.hh"
#include "Base64.hh"
#include <GroupsockHelper.hh>

////////// RTSPServer implementation //////////

MyRTSPServer*
MyRTSPServer::createNew(UsageEnvironment& env, Port ourPort,
                      UserAuthenticationDatabase* authDatabase,
                      unsigned reclamationSeconds) {
    int ourSocket = setUpOurSocket(env, ourPort);
    if (ourSocket == -1) return NULL;

    return new MyRTSPServer(env, ourSocket, ourPort, authDatabase, reclamationSeconds);
}

char* MyRTSPServer
::rtspURL(ServerMediaSession const* serverMediaSession, int clientSocket) const {
    char* urlPrefix = rtspURLPrefix(clientSocket);
    char const* sessionName = serverMediaSession->streamName();

    char* resultURL = new char[strlen(urlPrefix) + strlen(sessionName) + 1];
    sprintf(resultURL, "%s%s", urlPrefix, sessionName);

    delete[] urlPrefix;
    return resultURL;
}

char* MyRTSPServer::rtspURLPrefix(int clientSocket) const {
    struct sockaddr_in ourAddress;
    if (clientSocket < 0) {
        // Use our default IP address in the URL:
        ourAddress.sin_addr.s_addr = ReceivingInterfaceAddr != 0
            ? ReceivingInterfaceAddr
            : ourIPAddress(envir()); // hack
    } else {
        SOCKLEN_T namelen = sizeof ourAddress;
        getsockname(clientSocket, (struct sockaddr*)&ourAddress, &namelen);
    }

    char urlBuffer[100]; // more than big enough for "rtsp://<ip-address>:<port>/"

    portNumBits portNumHostOrder = ntohs(fServerPort.num());
    if (portNumHostOrder == 554 /* the default port number */) {
        sprintf(urlBuffer, "rtsp://%s/", AddressString(ourAddress).val());
    } else {
        sprintf(urlBuffer, "rtsp://%s:%hu/",
            AddressString(ourAddress).val(), portNumHostOrder);
    }

    return strDup(urlBuffer);
}

UserAuthenticationDatabase* MyRTSPServer::setAuthenticationDatabase(UserAuthenticationDatabase* newDB) {
    UserAuthenticationDatabase* oldDB = fAuthDB;
    fAuthDB = newDB;

    return oldDB;
}

Boolean MyRTSPServer::setUpTunnelingOverHTTP(Port httpPort) {
    fHTTPServerSocket = setUpOurSocket(envir(), httpPort);
    if (fHTTPServerSocket >= 0) {
        fHTTPServerPort = httpPort;
        envir().taskScheduler().turnOnBackgroundReadHandling(fHTTPServerSocket,
            incomingConnectionHandlerHTTP, this);
        return True;
    }

    return False;
}

portNumBits MyRTSPServer::httpServerPortNum() const {
    return ntohs(fHTTPServerPort.num());
}

char const* MyRTSPServer::allowedCommandNames() {
    return "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER";
}

UserAuthenticationDatabase* MyRTSPServer::getAuthenticationDatabaseForCommand(char const* /*cmdName*/) {
    // default implementation
    return fAuthDB;
}

Boolean MyRTSPServer::specialClientAccessCheck(int /*clientSocket*/, struct sockaddr_in& /*clientAddr*/, char const* /*urlSuffix*/) {
    // default implementation
    return True;
}

Boolean MyRTSPServer::specialClientUserAccessCheck(int /*clientSocket*/, struct sockaddr_in& /*clientAddr*/,
                                                 char const* /*urlSuffix*/, char const * /*username*/) {
    // default implementation; no further access restrictions:
    return True;
}


MyRTSPServer::MyRTSPServer(UsageEnvironment& env,
                       int ourSocket, Port ourPort,
                       UserAuthenticationDatabase* authDatabase,
                       unsigned reclamationSeconds)
                       : GenericMediaServer(env, ourSocket, ourPort, reclamationSeconds),
                       fHTTPServerSocket(-1), fHTTPServerPort(0),
                       fClientConnectionsForHTTPTunneling(NULL), // will get created if needed
                       fTCPStreamingDatabase(HashTable::create(ONE_WORD_HASH_KEYS)),
                       fAuthDB(authDatabase), fAllowStreamingRTPOverTCP(True) {
}

// A data structure that is used to implement "fTCPStreamingDatabase"
// (and the "noteTCPStreamingOnSocket()" and "stopTCPStreamingOnSocket()" member functions):
class streamingOverTCPRecord {
public:
    streamingOverTCPRecord(u_int32_t sessionId, unsigned trackNum, streamingOverTCPRecord* next)
        : fNext(next), fSessionId(sessionId), fTrackNum(trackNum) {
    }
    virtual ~streamingOverTCPRecord() {
        delete fNext;
    }

    streamingOverTCPRecord* fNext;
    u_int32_t fSessionId;
    unsigned fTrackNum;
};

MyRTSPServer::~MyRTSPServer() {
    // Turn off background HTTP read handling (if any):
    envir().taskScheduler().turnOffBackgroundReadHandling(fHTTPServerSocket);
    ::closeSocket(fHTTPServerSocket);

    cleanup(); // Removes all "ClientSession" and "ClientConnection" objects, and their tables.
    delete fClientConnectionsForHTTPTunneling;

    // Empty out and close "fTCPStreamingDatabase":
    streamingOverTCPRecord* sotcp;
    while ((sotcp = (streamingOverTCPRecord*)fTCPStreamingDatabase->getFirst()) != NULL) {
        delete sotcp;
    }
    delete fTCPStreamingDatabase;
}

Boolean MyRTSPServer::isRTSPServer() const {
    return True;
}

void MyRTSPServer::incomingConnectionHandlerHTTP(void* instance, int /*mask*/) {
    MyRTSPServer* server = (MyRTSPServer*)instance;
    server->incomingConnectionHandlerHTTP();
}
void MyRTSPServer::incomingConnectionHandlerHTTP() {
    incomingConnectionHandlerOnSocket(fHTTPServerSocket);
}
