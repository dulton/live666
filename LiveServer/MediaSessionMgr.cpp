
#include "GenericMediaServer.hh"
#include <GroupsockHelper.hh>
#if defined(__WIN32__) || defined(_WIN32) || defined(_QNX4)
#define snprintf _snprintf
#endif

////////// GenericMediaServer implementation //////////

void MediaSessionMgr::addServerMediaSession(ServerMediaSession* serverMediaSession) {
    if (serverMediaSession == NULL) return;

    char const* sessionName = serverMediaSession->streamName();
    if (sessionName == NULL) sessionName = "";
    removeServerMediaSession(sessionName); // in case an existing "ServerMediaSession" with this name already exists

    m_mMediaSessions.insert(MediaSessionType(sessionName, (void*)serverMediaSession));
}

ServerMediaSession* MediaSessionMgr
::lookupServerMediaSession(char const* streamName, Boolean /*isFirstLookupInSession*/) {
    // Default implementation:
    //lock
    return (ServerMediaSession*)(fServerMediaSessions->Lookup(streamName));
}

void MediaSessionMgr::removeServerMediaSession(ServerMediaSession* serverMediaSession) {
    if (serverMediaSession == NULL) return;
    
    //lock
    fServerMediaSessions->Remove(serverMediaSession->streamName());
    if (serverMediaSession->referenceCount() == 0) {
        Medium::close(serverMediaSession);
    } else {
        serverMediaSession->deleteWhenUnreferenced() = True;
    }
}

void MediaSessionMgr::removeServerMediaSession(char const* streamName) {
    removeServerMediaSession((ServerMediaSession*)(fServerMediaSessions->Lookup(streamName)));
}

void MediaSessionMgr::closeAllClientSessionsForServerMediaSession(ServerMediaSession* serverMediaSession) {
    if (serverMediaSession == NULL) return;

    HashTable::Iterator* iter = HashTable::Iterator::create(*fClientSessions);
    GenericMediaServer::ClientSession* clientSession;
    char const* key; // dummy
    while ((clientSession = (GenericMediaServer::ClientSession*)(iter->next(key))) != NULL) {
        if (clientSession->fOurServerMediaSession == serverMediaSession) {
            delete clientSession;
        }
    }
    delete iter;
}

void MediaSessionMgr::closeAllClientSessionsForServerMediaSession(char const* streamName) {
    closeAllClientSessionsForServerMediaSession((ServerMediaSession*)(fServerMediaSessions->Lookup(streamName)));
}

void MediaSessionMgr::deleteServerMediaSession(ServerMediaSession* serverMediaSession) {
    if (serverMediaSession == NULL) return;

    closeAllClientSessionsForServerMediaSession(serverMediaSession);
    removeServerMediaSession(serverMediaSession);
}

void MediaSessionMgr::deleteServerMediaSession(char const* streamName) {
    deleteServerMediaSession((ServerMediaSession*)(fServerMediaSessions->Lookup(streamName)));
}

MediaSessionMgr
::MediaSessionMgr(UsageEnvironment& env, int ourSocket, Port ourPort,
                     unsigned reclamationSeconds)
                     : Medium(env),
                     fServerSocket(ourSocket), fServerPort(ourPort), fReclamationSeconds(reclamationSeconds),
                     fServerMediaSessions(HashTable::create(STRING_HASH_KEYS)),
                     fClientConnections(HashTable::create(ONE_WORD_HASH_KEYS)),
                     fClientSessions(HashTable::create(STRING_HASH_KEYS)) {
                         ignoreSigPipeOnSocket(fServerSocket); // so that clients on the same host that are killed don't also kill us

                         // Arrange to handle connections from others:
                         env.taskScheduler().turnOnBackgroundReadHandling(fServerSocket, incomingConnectionHandler, this);
}

MediaSessionMgr::~MediaSessionMgr() {
}

void MediaSessionMgr::cleanup() {
    // Delete all server media sessions
    ServerMediaSession* serverMediaSession;
    while ((serverMediaSession = (ServerMediaSession*)fServerMediaSessions->getFirst()) != NULL) {
        removeServerMediaSession(serverMediaSession); // will delete it, because it no longer has any 'client session' objects using it
    }
    delete fServerMediaSessions;
}


