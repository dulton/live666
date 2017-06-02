
#include "MyRTSPServer.h"
#include "RTSPCommon.hh"
#include "Base64.hh"
#include <GroupsockHelper.hh>
#include <BasicUsageEnvironment.hh>
#include "MyRTSPClientConnection.h"

////////// RTSPServer implementation //////////

#define LISTEN_BACKLOG_SIZE 20

MyRTSPServer::MyRTSPServer(MediaSessionMgr& MediaMgr,UsageEnvironment& env, Port ourPort):Medium(env),
fMediaMgr(MediaMgr), fHTTPServerSocket(-1), fHTTPServerPort(ourPort)
{
    fHTTPServerSocket = setUpOurSocket(env, ourPort);
    if (fHTTPServerSocket == -1) return ;
    ignoreSigPipeOnSocket(fHTTPServerSocket); // so that clients on the same host that are killed don't also kill us

    // Arrange to handle connections from others:
    envir().taskScheduler().turnOnBackgroundReadHandling(fHTTPServerSocket, incomingConnectionHandler, this);

}

int MyRTSPServer::setUpOurSocket(UsageEnvironment& env, Port& ourPort) {
    int ourSocket = -1;

    do {
        // The following statement is enabled by default.
        // Don't disable it (by defining ALLOW_SERVER_PORT_REUSE) unless you know what you're doing.
#if !defined(ALLOW_SERVER_PORT_REUSE) && !defined(ALLOW_RTSP_SERVER_PORT_REUSE)
        // ALLOW_RTSP_SERVER_PORT_REUSE is for backwards-compatibility #####
        NoReuse dummy(env); // Don't use this socket if there's already a local server using it
#endif

        ourSocket = setupStreamSocket(env, ourPort);
        if (ourSocket < 0) break;

        // Make sure we have a big send buffer:
        if (!increaseSendBufferTo(env, ourSocket, 50*1024)) break;

        // Allow multiple simultaneous connections:
        if (listen(ourSocket, LISTEN_BACKLOG_SIZE) < 0) {
            env.setResultErrMsg("listen() failed: ");
            break;
        }

        if (ourPort.num() == 0) {
            // bind() will have chosen a port for us; return it also:
            if (!getSourcePort(env, ourSocket, ourPort)) break;
        }

        return ourSocket;
    } while (0);

    if (ourSocket != -1) ::closeSocket(ourSocket);
    return -1;
}

portNumBits MyRTSPServer::httpServerPortNum() const {
    return ntohs(fHTTPServerPort.num());
}

MyRTSPServer::~MyRTSPServer() {
    // Turn off background HTTP read handling (if any):
    envir().taskScheduler().turnOffBackgroundReadHandling(fHTTPServerSocket);
    ::closeSocket(fHTTPServerSocket);
}

Boolean MyRTSPServer::isRTSPServer() const {
    return True;
}

void MyRTSPServer::incomingConnectionHandler(void* instance, int /*mask*/)
{
    MyRTSPServer* server = (MyRTSPServer*)instance;
    server->incomingConnectionHandlerOnSocket();
}

void MyRTSPServer::incomingConnectionHandlerOnSocket() {
    struct sockaddr_in clientAddr;
    SOCKLEN_T clientAddrLen = sizeof clientAddr;
    int clientSocket = accept(fHTTPServerSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (clientSocket < 0) {
        int err = envir().getErrno();
        if (err != EWOULDBLOCK) {
            envir().setResultErrMsg("accept() failed: ");
        }
        return;
    }
    ignoreSigPipeOnSocket(clientSocket); // so that clients on the same host that are killed don't also kill us
    makeSocketNonBlocking(clientSocket);
    increaseSendBufferTo(envir(), clientSocket, 50*1024);

#ifdef DEBUG
    envir() << "accept()ed connection from " << AddressString(clientAddr).val() << "\n";
#endif

    //这里指望MyRTSPClientConnection自己删除自己。不知道行不行
    new  MyRTSPClientConnection(fMediaMgr, clientSocket, clientAddr);
}

int MyRTSPServer::Run()
{
    envir().taskScheduler().doEventLoop();
    return 0;
}