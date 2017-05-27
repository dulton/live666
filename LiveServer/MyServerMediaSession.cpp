#include "MyServerMediaSession.hh"
#include <GroupsockHelper.hh>
#include <math.h>
#if defined(__WIN32__) || defined(_WIN32) || defined(_QNX4)
#define snprintf _snprintf
#endif

////////// ServerMediaSession //////////

MyServerMediaSession* MyServerMediaSession
::createNew(UsageEnvironment& env,
	    char const* streamName, char const* info,
	    char const* description, Boolean isSSM, char const* miscSDPLines) {
  return new MyServerMediaSession(env, streamName, info, description,
				isSSM, miscSDPLines);
}

static char const* const libNameStr = "LIVE666 Streaming Media v";
char const* const libVersionStr = LIVEMEDIA_LIBRARY_VERSION_STRING;

MyServerMediaSession::MyServerMediaSession(UsageEnvironment& env,
				       char const* streamName,
				       char const* info,
				       char const* description,
				       Boolean isSSM, char const* miscSDPLines)
  : Medium(env), fIsSSM(isSSM), 
    fReferenceCount(0), fDeleteWhenUnreferenced(False) {
  fStreamName = strDup(streamName == NULL ? "" : streamName);

  char* libNamePlusVersionStr = NULL; // by default
  if (info == NULL || description == NULL) {
    libNamePlusVersionStr = new char[strlen(libNameStr) + strlen(libVersionStr) + 1];
    sprintf(libNamePlusVersionStr, "%s%s", libNameStr, libVersionStr);
  }
  fInfoSDPString = strDup(info == NULL ? libNamePlusVersionStr : info);
  fDescriptionSDPString = strDup(description == NULL ? libNamePlusVersionStr : description);
  delete[] libNamePlusVersionStr;

  fMiscSDPLines = strDup(miscSDPLines == NULL ? "" : miscSDPLines);

  gettimeofday(&fCreationTime, NULL);
}

MyServerMediaSession::~MyServerMediaSession() {
  deleteAllSubsessions();
}

Boolean
MyServerMediaSession::addSubsession(MyServerMediaSubsession* subsession) {
  subSessions.push_back(subsession);
  return True;
}

void MyServerMediaSession::testScaleFactor(float& scale) {
  // First, try setting all subsessions to the desired scale.
  // If the subsessions' actual scales differ from each other, choose the
  // value that's closest to 1, and then try re-setting all subsessions to that
  // value.  If the subsessions' actual scales still differ, re-set them all to 1.
  float minSSScale = 1.0;
  float maxSSScale = 1.0;
  float bestSSScale = 1.0;
  float bestDistanceTo1 = 0.0;

  MyServerMediaSubsession* subsession = NULL;
  for (SubIterator it = subSessions.begin(); it != subSessions.end(); it++){
      subsession = *it;

      float ssscale = scale;
      subsession->testScaleFactor(ssscale);
      if (it == subSessions.begin()) { // this is the first subsession
          minSSScale = maxSSScale = bestSSScale = ssscale;
          bestDistanceTo1 = (float)fabs(ssscale - 1.0f);
      } else {
          if (ssscale < minSSScale) {
              minSSScale = ssscale;
          } else if (ssscale > maxSSScale) {
              maxSSScale = ssscale;
          }

          float distanceTo1 = (float)fabs(ssscale - 1.0f);
          if (distanceTo1 < bestDistanceTo1) {
              bestSSScale = ssscale;
              bestDistanceTo1 = distanceTo1;
          }
      }
  }

  if (minSSScale == maxSSScale) {
    // All subsessions are at the same scale: minSSScale == bestSSScale == maxSSScale
    scale = minSSScale;
    return;
  }

  // The scales for each subsession differ.  Try to set each one to the value
  // that's closest to 1:
  subsession = NULL;
  for (SubIterator it = subSessions.begin(); it != subSessions.end(); it++){
      subsession = *it;
      float ssscale = bestSSScale;
      subsession->testScaleFactor(ssscale);
      if (ssscale != bestSSScale) break; // no luck
  }

  if (subsession == NULL) {
    // All subsessions are at the same scale: bestSSScale
    scale = bestSSScale;
    return;
  }

  // Still no luck.  Set each subsession's scale to 1:
  for (SubIterator it = subSessions.begin(); it != subSessions.end(); it++){
      subsession = *it;
      float ssscale = 1;
      subsession->testScaleFactor(ssscale);
  }

  scale = 1;
}

float MyServerMediaSession::duration() const {
  float minSubsessionDuration = 0.0;
  float maxSubsessionDuration = 0.0;
  MyServerMediaSubsession* subsession = NULL;
  for (SubIterator it = subSessions.begin(); it != subSessions.end(); it++){
       subsession = *it;
       // Hack: If any subsession supports seeking by 'absolute' time, then return a negative value, to indicate that only subsessions
       // will have a "a=range:" attribute:
       char* absStartTime = NULL; char* absEndTime = NULL;
       subsession->getAbsoluteTimeRange(absStartTime, absEndTime);
       if (absStartTime != NULL) return -1.0f;
 
       float ssduration = subsession->duration();
       if (it == subSessions.begin()) { // this is the first subsession
           minSubsessionDuration = maxSubsessionDuration = ssduration;
       } else if (ssduration < minSubsessionDuration) {
           minSubsessionDuration = ssduration;
       } else if (ssduration > maxSubsessionDuration) {
           maxSubsessionDuration = ssduration;
       }
  }
  if (maxSubsessionDuration != minSubsessionDuration) {
    return -maxSubsessionDuration; // because subsession durations differ
  } else {
    return maxSubsessionDuration; // all subsession durations are the same
  }
}

void MyServerMediaSession::noteLiveness() {
  // default implementation: do nothing
}

virtual void startStream(unsigned clientSessionId, void* streamToken,
                         TaskFunc* rtcpRRHandler,
                         void* rtcpRRHandlerClientData,
                         unsigned short& rtpSeqNum,
                         unsigned& rtpTimestamp,
                         ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
                         void* serverRequestAlternativeByteHandlerClientData){

}
virtual void pauseStream(unsigned clientSessionId, void* streamToken){
}
virtual void seekStream(unsigned clientSessionId, void* streamToken, double& seekNPT,
                        double streamDuration, u_int64_t& numBytes){
}
virtual void seekStream(unsigned clientSessionId, void* streamToken, char*& absStart, char*& absEnd){
}
virtual void nullSeekStream(unsigned clientSessionId, void* streamToken,
                            double streamEndTime, u_int64_t& numBytes){
}
virtual void setStreamScale(unsigned clientSessionId, void* streamToken, float scale){

}

void MyServerMediaSession::deleteAllSubsessions() {
    for (SubIterator it = subSessions.begin(); it != subSessions.end(); it++){
        Medium::close(*it);
    }
}

Boolean MyServerMediaSession::authenticationOK(string cmdName, string uri, string UserName, string Password,
                                               string& response)
{
#ifdef DEBUG
    fprintf(stderr, "lookupPassword(%s) returned password %s\n", username, password);
#endif
    if (password == NULL) break;
    fAuth.setUsernameAndPassword(UserName.c_str(), Password.c_str(), passwordIsMd5);

    // Finally, compute a digest response from the information that we have,
    // and compare it to the one that we were given:
    char const* ourResponse
        = fAuth.computeDigestResponse(cmdName, uri);
    Boolean success = (strcmp(ourResponse, response) == 0);
    fAuth.reclaimDigestResponse(ourResponse);
    return success;
}

Boolean MyServerMediaSession::isServerMediaSession() const {
  return True;
}

string MyServerMediaSession::generateSDPDescription() {
  AddressString ipAddressStr(ourIPAddress(envir()));
  unsigned ipAddressStrSize = strlen(ipAddressStr.val());

  // For a SSM sessions, we need a "a=source-filter: incl ..." line also:
  char* sourceFilterLine;
  if (fIsSSM) {
    char const* const sourceFilterFmt =
      "a=source-filter: incl IN IP4 * %s\r\n"
      "a=rtcp-unicast: reflection\r\n";
    unsigned const sourceFilterFmtSize = strlen(sourceFilterFmt) + ipAddressStrSize + 1;

    sourceFilterLine = new char[sourceFilterFmtSize];
    sprintf(sourceFilterLine, sourceFilterFmt, ipAddressStr.val());
  } else {
    sourceFilterLine = strDup("");
  }

  char* rangeLine = NULL; // for now
  char* sdp = NULL; // for now

  do {
    // Count the lengths of each subsession's media-level SDP lines.
    // (We do this first, because the call to "subsession->sdpLines()"
    // causes correct subsession 'duration()'s to be calculated later.)
    unsigned sdpLength = 0;
    for (SubIterator it = subSessions.begin(); it != subSessions.end(); it++){
        char const* sdpLines = (*it)->sdpLines();
        if (sdpLines == NULL) continue; // the media's not available
        sdpLength += strlen(sdpLines);
    }

    if (sdpLength == 0) break; // the session has no usable subsessions

    // Unless subsessions have differing durations, we also have a "a=range:" line:
    float dur = duration();
    if (dur == 0.0) {
      rangeLine = strDup("a=range:npt=0-\r\n");
    } else if (dur > 0.0) {
      char buf[100];
      sprintf(buf, "a=range:npt=0-%.3f\r\n", dur);
      rangeLine = strDup(buf);
    } else { // subsessions have differing durations, so "a=range:" lines go there
      rangeLine = strDup("");
    }

    char const* const sdpPrefixFmt =
      "v=0\r\n"
      "o=- %ld%06ld %d IN IP4 %s\r\n"
      "s=%s\r\n"
      "i=%s\r\n"
      "t=0 0\r\n"
      "a=tool:%s%s\r\n"
      "a=type:broadcast\r\n"
      "a=control:*\r\n"
      "%s"
      "%s"
      "a=x-qt-text-nam:%s\r\n"
      "a=x-qt-text-inf:%s\r\n"
      "%s";
    sdpLength += strlen(sdpPrefixFmt)
      + 20 + 6 + 20 + ipAddressStrSize
      + fDescriptionSDPString.length()
      + fInfoSDPString.length()
      + strlen(libNameStr) + strlen(libVersionStr)
      + strlen(sourceFilterLine)
      + strlen(rangeLine)
      + fDescriptionSDPString.length()
      + fInfoSDPString.length()
      + fMiscSDPLines.length();
    sdpLength += 1000; // in case the length of the "subsession->sdpLines()" calls below change
    sdp = new char[sdpLength];
    if (sdp == NULL) break;

    // Generate the SDP prefix (session-level lines):
    snprintf(sdp, sdpLength, sdpPrefixFmt,
	     fCreationTime.tv_sec, fCreationTime.tv_usec, // o= <session id>
	     1, // o= <version> // (needs to change if params are modified)
	     ipAddressStr.val(), // o= <address>
	     fDescriptionSDPString, // s= <description>
	     fInfoSDPString, // i= <info>
	     libNameStr, libVersionStr, // a=tool:
	     sourceFilterLine, // a=source-filter: incl (if a SSM session)
	     rangeLine, // a=range: line
	     fDescriptionSDPString, // a=x-qt-text-nam: line
	     fInfoSDPString, // a=x-qt-text-inf: line
	     fMiscSDPLines); // miscellaneous session SDP lines (if any)

    // Then, add the (media-level) lines for each subsession:
    char* mediaSDP = sdp;
    for (SubIterator it = subSessions.begin(); it != subSessions.end(); it++){
        unsigned mediaSDPLength = strlen(mediaSDP);
        mediaSDP += mediaSDPLength;
        sdpLength -= mediaSDPLength;
        if (sdpLength <= 1) break; // the SDP has somehow become too long

        char const* sdpLines = (*it)->sdpLines();
        if (sdpLines != NULL) snprintf(mediaSDP, sdpLength, "%s", sdpLines);
    }
  } while (0);
  
  string retSdp = sdp;
  delete[] rangeLine; delete[] sourceFilterLine; delete[] sdp;
  return sdp;
}

////////// ServerMediaSubsession //////////

MyServerMediaSubsession::MyServerMediaSubsession(UsageEnvironment& env)
  : Medium(env),
    fServerAddressForSDP(0), fPortNumForSDP(0),
    fTrackNumber(0), fTrackId(NULL) {
}

MyServerMediaSubsession::~MyServerMediaSubsession() {
}

string MyServerMediaSubsession::trackId() {
  if (fTrackNumber == 0) return NULL; // not yet in a ServerMediaSession

  if (fTrackId.empty()) {
    char buf[100];
    sprintf(buf, "track%d", fTrackNumber);
    fTrackId = strDup(buf);
  }
  return fTrackId;
}

void MyServerMediaSubsession::pauseStream(unsigned /*clientSessionId*/,
					void* /*streamToken*/) {
  // default implementation: do nothing
}
void MyServerMediaSubsession::seekStream(unsigned /*clientSessionId*/,
				       void* /*streamToken*/, double& /*seekNPT*/, double /*streamDuration*/, u_int64_t& numBytes) {
  // default implementation: do nothing
  numBytes = 0;
}
void MyServerMediaSubsession::seekStream(unsigned /*clientSessionId*/,
				       void* /*streamToken*/, char*& absStart, char*& absEnd) {
  // default implementation: do nothing (but delete[] and assign "absStart" and "absEnd" to NULL, to show that we don't handle this)
  delete[] absStart; absStart = NULL;
  delete[] absEnd; absEnd = NULL;
}
void MyServerMediaSubsession::nullSeekStream(unsigned /*clientSessionId*/, void* /*streamToken*/,
					   double streamEndTime, u_int64_t& numBytes) {
  // default implementation: do nothing
  numBytes = 0;
}
void MyServerMediaSubsession::setStreamScale(unsigned /*clientSessionId*/,
					   void* /*streamToken*/, float /*scale*/) {
  // default implementation: do nothing
}
float MyServerMediaSubsession::getCurrentNPT(void* /*streamToken*/) {
  // default implementation: return 0.0
  return 0.0;
}
FramedSource* MyServerMediaSubsession::getStreamSource(void* /*streamToken*/) {
  // default implementation: return NULL
  return NULL;
}
void MyServerMediaSubsession::deleteStream(unsigned /*clientSessionId*/,
					 void*& /*streamToken*/) {
  // default implementation: do nothing
}

void MyServerMediaSubsession::testScaleFactor(float& scale) {
  // default implementation: Support scale = 1 only
  scale = 1;
}

float MyServerMediaSubsession::duration() const {
  // default implementation: assume an unbounded session:
  return 0.0;
}

void MyServerMediaSubsession::getAbsoluteTimeRange(char*& absStartTime, char*& absEndTime) const {
  // default implementation: We don't support seeking by 'absolute' time, so indicate this by setting both parameters to NULL:
  absStartTime = absEndTime = NULL;
}

void MyServerMediaSubsession::setServerAddressAndPortForSDP(netAddressBits addressBits,
							  portNumBits portBits) {
  fServerAddressForSDP = addressBits;
  fPortNumForSDP = portBits;
}

string
MyServerMediaSubsession::rangeSDPLine() const {
  // First, check for the special case where we support seeking by 'absolute' time:
  char* absStart = NULL; char* absEnd = NULL;
  getAbsoluteTimeRange(absStart, absEnd);
  if (absStart != NULL) {
    char buf[100];

    if (absEnd != NULL) {
      sprintf(buf, "a=range:clock=%s-%s\r\n", absStart, absEnd);
    } else {
      sprintf(buf, "a=range:clock=%s-\r\n", absStart);
    }
    return string(buf);
  }

  // Use our own duration for a "a=range:" line:
  float ourDuration = duration();
  if (ourDuration == 0.0) {
    return string("a=range:npt=0-\r\n");
  } else {
    char buf[100];
    sprintf(buf, "a=range:npt=0-%.3f\r\n", ourDuration);
    return string(buf);
  }
}
