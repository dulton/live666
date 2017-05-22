#pragma  once
#include "DigestAuthentication.hh"


#include "Media.hh"
#include "ServerMediaSession.hh"

#ifndef REQUEST_BUFFER_SIZE
#define REQUEST_BUFFER_SIZE 20000 // for incoming requests
#endif
#ifndef RESPONSE_BUFFER_SIZE
#define RESPONSE_BUFFER_SIZE 20000
#endif

class MyRTSPServer: public Medium {
public:
    static MyRTSPServer* createNew(UsageEnvironment& env, Port ourPort = 554,
        UserAuthenticationDatabase* authDatabase = NULL,
        unsigned reclamationSeconds = 65);

    char* rtspURL(ServerMediaSession const* serverMediaSession, int clientSocket = -1) const;

    char* rtspURLPrefix(int clientSocket = -1) const;

    UserAuthenticationDatabase* setAuthenticationDatabase(UserAuthenticationDatabase* newDB);

    void disableStreamingRTPOverTCP() {
        fAllowStreamingRTPOverTCP = False;
    }

    Boolean setUpTunnelingOverHTTP(Port httpPort);

    portNumBits httpServerPortNum() const; // in host byte order.  (Returns 0 if not present.)

protected:
    MyRTSPServer(UsageEnvironment& env,
        int ourSocket, Port ourPort,
        UserAuthenticationDatabase* authDatabase,
        unsigned reclamationSeconds);
    // called only by createNew();
    virtual ~RTSPServer();

    virtual char const* allowedCommandNames(); // used to implement "RTSPClientConnection::handleCmd_OPTIONS()"
    virtual Boolean weImplementREGISTER(char const* cmd/*"REGISTER" or "DEREGISTER"*/,
        char const* proxyURLSuffix, char*& responseStr);
    // used to implement "RTSPClientConnection::handleCmd_REGISTER()"
    // Note: "responseStr" is dynamically allocated (or NULL), and should be delete[]d after the call
    virtual UserAuthenticationDatabase* getAuthenticationDatabaseForCommand(char const* cmdName);
    virtual Boolean specialClientAccessCheck(int clientSocket, struct sockaddr_in& clientAddr,
        char const* urlSuffix);
    // a hook that allows subclassed servers to do server-specific access checking
    // on each client (e.g., based on client IP address), without using digest authentication.
    virtual Boolean specialClientUserAccessCheck(int clientSocket, struct sockaddr_in& clientAddr,
        char const* urlSuffix, char const *username);
    // another hook that allows subclassed servers to do server-specific access checking
    // - this time after normal digest authentication has already taken place (and would otherwise allow access).
    // (This test can only be used to further restrict access, not to grant additional access.)

private: // redefined virtual functions
    virtual Boolean isRTSPServer() const;

public: // should be protected, but some old compilers complain otherwise


protected: // redefined virtual functions
    // If you subclass "RTSPClientConnection", then you must also redefine this virtual function in order
    // to create new objects of your subclass:
    virtual ClientConnection* createNewClientConnection(int clientSocket, struct sockaddr_in clientAddr);

protected:
    // If you subclass "RTSPClientSession", then you must also redefine this virtual function in order
    // to create new objects of your subclass:
    virtual ClientSession* createNewClientSession(u_int32_t sessionId);

private:
    static void incomingConnectionHandlerHTTP(void*, int /*mask*/);
    void incomingConnectionHandlerHTTP();

    void noteTCPStreamingOnSocket(int socketNum, RTSPClientSession* clientSession, unsigned trackNum);
    void unnoteTCPStreamingOnSocket(int socketNum, RTSPClientSession* clientSession, unsigned trackNum);
    void stopTCPStreamingOnSocket(int socketNum);

private:
    int fHTTPServerSocket; // for optional RTSP-over-HTTP tunneling
    Port fHTTPServerPort; // ditto
    HashTable* fClientConnectionsForHTTPTunneling; // maps client-supplied 'session cookie' strings to "RTSPClientConnection"s
    // (used only for optional RTSP-over-HTTP tunneling)
    HashTable* fTCPStreamingDatabase;
    UserAuthenticationDatabase* fAuthDB;
    Boolean fAllowStreamingRTPOverTCP; // by default, True
};
