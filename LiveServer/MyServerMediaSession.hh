#pragma  once

#include <vector>
#include <string>

#ifndef _RTCP_HH
#include "RTCP.hh"
#endif

using namespace std;

class MyServerMediaSubsession;

class MyServerMediaSession: public Medium {

    typedef std::vector<MyServerMediaSubsession*> SubType;
    typedef SubType::iterator SubIterator;

public:
  static MyServerMediaSession* createNew(UsageEnvironment& env,
				       char const* streamName = NULL,
				       char const* info = NULL,
				       char const* description = NULL,
				       Boolean isSSM = False,
				       char const* miscSDPLines = NULL);

  string generateSDPDescription(); // based on the entire session
      // Note: The caller is responsible for freeing the returned string

  string streamName() const { return fStreamName; }

  Boolean addSubsession(MyServerMediaSubsession* subsession);
  unsigned numSubsessions() const { return (unsigned int)subSessions.size(); }

  void testScaleFactor(float& scale); // sets "scale" to the actual supported scale
  float duration() const;
    // a result == 0 means an unbounded session (the default)
    // a result < 0 means: subsession durations differ; the result is -(the largest).
    // a result > 0 means: this is the duration of a bounded session

  virtual void noteLiveness();
    // called whenever a client - accessing this media - notes liveness.
    // The default implementation does nothing, but subclasses can redefine this - e.g., if you
    // want to remove long-unused "ServerMediaSession"s from the server.

  unsigned referenceCount() const { return fReferenceCount; }
  void incrementReferenceCount() { ++fReferenceCount; }
  void decrementReferenceCount() { if (fReferenceCount > 0) --fReferenceCount; }
  Boolean& deleteWhenUnreferenced() { return fDeleteWhenUnreferenced; }

  //Á÷¹ÜÀí
  virtual void startStream(unsigned clientSessionId, void* streamToken,
      TaskFunc* rtcpRRHandler,
      void* rtcpRRHandlerClientData,
      unsigned short& rtpSeqNum,
      unsigned& rtpTimestamp,
      ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
      void* serverRequestAlternativeByteHandlerClientData, string& rtpInfo);
  virtual void pauseStream(unsigned clientSessionId, void* streamToken);
  virtual void seekStream(unsigned clientSessionId, void* streamToken, double& seekNPT,
      double streamDuration, u_int64_t& numBytes);
  virtual void seekStream(unsigned clientSessionId, void* streamToken, char*& absStart, char*& absEnd);
  virtual void nullSeekStream(unsigned clientSessionId, void* streamToken,
      double streamEndTime, u_int64_t& numBytes);
  virtual void setStreamScale(unsigned clientSessionId, void* streamToken, float scale);
  virtual void deleteStream(unsigned clientSessionId, void*& streamToken);
  void deleteAllSubsessions();
    // Removes and deletes all subsessions added by "addSubsession()", returning us to an 'empty' state
    // Note: If you have already added this "ServerMediaSession" to a "RTSPServer" then, before calling this function,
    //   you must first close any client connections that use it,
    //   by calling "RTSPServer::closeAllClientSessionsForServerMediaSession()".

  Boolean authenticationOK(string cmdName, string uri, string UserName, string Password,
      string realm, string nonce,
      string& response);

protected:
  MyServerMediaSession(UsageEnvironment& env, char const* streamName,
		     char const* info, char const* description,
		     Boolean isSSM, char const* miscSDPLines);
  // called only by "createNew()"

  virtual ~MyServerMediaSession();

private: // redefined virtual functions
  virtual Boolean isServerMediaSession() const;

private:
  Boolean fIsSSM;

  struct timeval fCreationTime;
  unsigned fReferenceCount;
  Boolean fDeleteWhenUnreferenced;

  string           localIPAddr_; // IP XXX.XXX.XXX.XXX
  string           fStreamName;
  string           fInfoSDPString;
  string           fDescriptionSDPString;
  string           fMiscSDPLines;
  SubType          subSessions;
  string           destinationAddr;
  string           userName;
  string           password;
  Boolean          passwordIsMd5;
  Authenticator    fAuth; // used if access control is needed

};

class MyServerMediaSubsession: public Medium {
public:
  unsigned trackNumber() const { return fTrackNumber; }
  string trackId();
  virtual char const* sdpLines() = 0;
  virtual void getStreamParameters(unsigned clientSessionId, // in
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
				   Port& serverRTCPPort, // out
				   ) = 0;
  virtual void startStream(unsigned clientSessionId, void* streamToken,
			   TaskFunc* rtcpRRHandler,
			   void* rtcpRRHandlerClientData,
			   unsigned short& rtpSeqNum,
			   unsigned& rtpTimestamp,
			   ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
			   void* serverRequestAlternativeByteHandlerClientData) = 0;
  virtual void pauseStream(unsigned clientSessionId, void* streamToken);
  virtual void seekStream(unsigned clientSessionId, void* streamToken, double& seekNPT,
			  double streamDuration, u_int64_t& numBytes);
     // This routine is used to seek by relative (i.e., NPT) time.
     // "streamDuration", if >0.0, specifies how much data to stream, past "seekNPT".  (If <=0.0, all remaining data is streamed.)
     // "numBytes" returns the size (in bytes) of the data to be streamed, or 0 if unknown or unlimited.
  virtual void seekStream(unsigned clientSessionId, void* streamToken, char*& absStart, char*& absEnd);
     // This routine is used to seek by 'absolute' time.
     // "absStart" should be a string of the form "YYYYMMDDTHHMMSSZ" or "YYYYMMDDTHHMMSS.<frac>Z".
     // "absEnd" should be either NULL (for no end time), or a string of the same form as "absStart".
     // These strings may be modified in-place, or can be reassigned to a newly-allocated value (after delete[]ing the original).
  virtual void nullSeekStream(unsigned clientSessionId, void* streamToken,
			      double streamEndTime, u_int64_t& numBytes);
     // Called whenever we're handling a "PLAY" command without a specified start time.
  virtual void setStreamScale(unsigned clientSessionId, void* streamToken, float scale);
  virtual float getCurrentNPT(void* streamToken);
  virtual FramedSource* getStreamSource(void* streamToken);
  virtual void getRTPSinkandRTCP(void* streamToken,
				 RTPSink const*& rtpSink, RTCPInstance const*& rtcp) = 0;
     // Returns pointers to the "RTPSink" and "RTCPInstance" objects for "streamToken".
     // (This can be useful if you want to get the associated 'Groupsock' objects, for example.)
     // You must not delete these objects, or start/stop playing them; instead, that is done
     // using the "startStream()" and "deleteStream()" functions.
  virtual void deleteStream(unsigned clientSessionId, void*& streamToken);

  virtual void testScaleFactor(float& scale); // sets "scale" to the actual supported scale
  virtual float duration() const;
    // returns 0 for an unbounded session (the default)
    // returns > 0 for a bounded session
  virtual void getAbsoluteTimeRange(char*& absStartTime, char*& absEndTime) const;
    // Subclasses can reimplement this iff they support seeking by 'absolute' time.

  // The following may be called by (e.g.) SIP servers, for which the
  // address and port number fields in SDP descriptions need to be non-zero:
  void setServerAddressAndPortForSDP(netAddressBits addressBits,
				     portNumBits portBits);

public:
    int tcpSocketNum;
protected: // we're a virtual base class
  MyServerMediaSubsession(UsageEnvironment& env);
  virtual ~MyServerMediaSubsession();

  string rangeSDPLine() const;

  netAddressBits fServerAddressForSDP;
  portNumBits fPortNumForSDP;

private:
  unsigned fTrackNumber; // within an enclosing ServerMediaSession
  string fTrackId;
};

