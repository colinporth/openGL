// server.cpp
//{{{  includes
#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
  #define _WINSOCK_DEPRECATED_NO_WARNINGS
  #include <winsock2.h>
  #include <WS2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
#endif

#ifdef __linux__
  #include <unistd.h>
  #include <netdb.h>
  #include <fcntl.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/mman.h>
  #include <sys/wait.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>

  #define SOCKET int
#endif

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "../../shared/fmt/core.h"
#include "../../shared/utils/cLog.h"

using namespace std;
using namespace fmt;
//}}}

constexpr int kHttpPortNumber = 80;
constexpr int kRtspPortNumber = 554;

//{{{
class cHttpServer {
public:
  cHttpServer (uint16_t portNumber) : mPortNumber(portNumber) {}
  ~cHttpServer() {}

  //{{{
  void start() {

    #ifdef _WIN32
      WSADATA wsaData;
      WSAStartup (MAKEWORD(2, 2), &wsaData);
    #endif

    mParentSocket = socket (AF_INET, SOCK_STREAM, 0);
    if (mParentSocket < 0)
      cLog::log (LOGERROR, "socket open failed");

    // allows us to restart server immediately
    int optval = 1;
    setsockopt (mParentSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval , sizeof(int));

    // bind port to socket
    struct sockaddr_in serverAddr;
    memset (&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl (INADDR_ANY);
    serverAddr.sin_port = htons (mPortNumber);

    if (bind (mParentSocket, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) < 0)
      cLog::log (LOGERROR, "bind failed");

    // ready to accept connection requests
    if (listen (mParentSocket, 5) < 0) // allow 5 requests to queue up
      cLog::log (LOGERROR, "listen failed");
    }
  //}}}
  //{{{
  SOCKET client (struct sockaddr_in& clientAddr) {

    socklen_t clientlen = sizeof (clientAddr);
    return ::accept (mParentSocket, (struct sockaddr*)&clientAddr, &clientlen);
    }
  //}}}

private:
  SOCKET mParentSocket;
  const uint16_t mPortNumber;
  };
//}}}
//{{{
class cHttpRequest {
public:
  cHttpRequest (SOCKET socket, const struct sockaddr_in& clientAddr, bool debug = false)
    : mSocket(socket), mSockAddrIn(clientAddr), mDebug(debug) {}
  ~cHttpRequest() {}

  string getMethod() { return mRequestStrings.size() > 0 ? mRequestStrings[0] : "no method"; }
  string getUri() { return mRequestStrings.size() > 1 ? mRequestStrings[1] : "no uri"; }
  string getVersion() { return mRequestStrings.size() > 2 ? mRequestStrings[2] : "no version"; }
  //{{{
  string getClientName() {
  // determine who sent the message

    struct hostent* hostEnt = gethostbyaddr ((const char*)&mSockAddrIn.sin_addr.s_addr,
                                             sizeof(mSockAddrIn.sin_addr.s_addr), AF_INET);
    // other hostent fields
    // char** h_aliases;
    // short h_addrtype;
    // short h_length;
    // char** h_addr_list;

    return hostEnt ? string(hostEnt->h_name) : string ("no clientName");
    }
  //}}}
  //{{{
  string getClientAddressString() {

    char* str = inet_ntoa (mSockAddrIn.sin_addr);
    return str ? string (str) : string("no clientAddr");
    }
  //}}}

  //{{{
  bool receive() {
  // crude http request parser

    constexpr int kRecvBufferSize = 1024;
    uint8_t buffer[kRecvBufferSize];

    bool needMoreData = true;
    while (needMoreData) {
      auto bufferPtr = buffer;
      auto bufferBytesReceived =  recv (mSocket, (char*)buffer, kRecvBufferSize, 0);;
      if (bufferBytesReceived <= 0) {
        cLog::log (LOGERROR, "recv terminated with no bytes %d", bufferBytesReceived);
        break;
        }
      while (needMoreData && (bufferBytesReceived > 0)) {
        int bytesReceived;
        needMoreData = parseData (bufferPtr, bufferBytesReceived, bytesReceived);
        bufferBytesReceived -= bytesReceived;
        bufferPtr += bytesReceived;
        }
      }

    if (!mStrings.empty())
      mRequestStrings = split (mStrings[0], ' ');

    if (mStrings.size() > 1) {
      for (size_t i = 1; i < mStrings.size(); i++) {
        vector <string> strings = splitHeader (mStrings[i]);
        if (strings.size() == 2)
          mHeaders.push_back (cHeader (strings[0], strings[1]));
        }
      }

    if (mDebug)
      report (true);

    return !mStrings.empty() && (mRequestStrings.size() == 3);
    }
  //}}}

  //{{{
  bool respondFile() {

    #ifdef __linux__
      string uri = "." + getUri();
    #else
      string uri = "E:/piccies" + getUri();
    #endif

    int fileSize = getFileSize (uri);
    if (fileSize) {
      sendResponseOK (uri, fileSize);

      // read file into fileBuffer
      FILE* file = fopen (uri.c_str(), "rb");
      uint8_t* fileBuffer = (uint8_t*)malloc (fileSize);
      fread (fileBuffer, 1, fileSize, file);
      fclose (file);

      // send file from fileBuffer
      if (send (mSocket, (const char*)fileBuffer, (int)fileSize, 0) < 0)
        cLog::log (LOGERROR, "send failed");
      free (fileBuffer);

      cLog::log (LOGINFO, format ("file {} sent", uri));

      closeSocket();
      return true;
      }

    return false;
    }
  //}}}
  //{{{
  void respondNotOk() {

    string response = format ("HTTP/1.1 404 notFound\n"
                              "Content-type: text/html\n"
                              "\n"
                              "<html><title>Tiny Error</title>"
                              "<body bgcolor=""ffffff"">\n"
                              "404: notFound\n"
                              "<p>Tiny couldn't find this file: {}\n"
                              "<hr><em>Colin web server</em>\n",
                              getUri());

    if (send (mSocket, response.c_str(), (int)response.size(), 0) < 0)
      cLog::log (LOGERROR, "sendResponseNotOk send failed");

    closeSocket();

    cLog::log (LOGINFO, format ("404 - file {} not found", getUri()));
    }
  //}}}

  //{{{
  void report (bool showHeaders) {

    cLog::log (LOGINFO1, format ("{} {} {} numLines:{} numHeaders:{}",
                                 getMethod(), getUri(), getVersion(), mStrings.size(), mHeaders.size()));

    if (showHeaders)
      for (auto& header : mHeaders)
        cLog::log (LOGINFO1, format ("tag:{} value:{}", header.mTag, header.mValue));
    }
  //}}}

private:
  //{{{
  static int getFileSize (const string& filename) {

  #ifdef _WIN32
    struct _stati64 st;
    if (_stat64 (filename.c_str(), &st) == -1)
  #else
    struct stat st;
    if (stat (filename.c_str(), &st) == -1)
  #endif
      return 0;
    else
      return (int)st.st_size;
    }
  //}}}
  //{{{
  static vector<string> split (const string& lineString, char delimiter = ' ') {

    vector <string> strings;

    size_t previous = 0;
    size_t current = lineString.find (delimiter);
    while (current != string::npos) {
      strings.push_back (lineString.substr (previous, current - previous));
      previous = current + 1;
      current = lineString.find (delimiter, previous);
      }

    strings.push_back (lineString.substr (previous, current - previous));

    return strings;
    }
  //}}}
  //{{{
  static vector<string> splitHeader (const string& lineString) {

    vector <string> strings;

    size_t current = lineString.find (':');
    if (current != string::npos) {
      strings.push_back (lineString.substr (0, current));
      strings.push_back (lineString.substr (current+2, string::npos));
      }

    return strings;
    }
  //}}}

  //{{{
  bool parseData (const uint8_t* data, int length, int& bytesParsed) {
  // crude http line parser

    int initialLength = length;

    while (length) {
      char ch = *data;
      switch (mState) {
        case eDone:
        case eNone:
        case eLine:
        case eError:
          mString = "";
        case eChar:
          if (ch =='\r') // return, expect newline
            mState = eReturn;
          else if (ch =='\n') {
            // newline before return, error
            cLog::log (LOGERROR, "newline before return");
            mState = eError;
            }
          else {// add to string
            mString += ch;
            mState = eChar;
            }
          break;

        case eReturn:
          if (ch == '\n') {
            // newline after return
            if (mString.empty()) {
              // empty line, done
              mState = eDone;
              bytesParsed = initialLength - length;
              return false;
              }
            else {
              mState = eLine;
              mStrings.push_back (mString);
              if (mDebug)
                cLog::log (LOGINFO, mString);
              }
            }
          else {
            // not newline after return
            mState = eError;
            cLog::log (LOGERROR, "not newline after return %c", ch);
            }
          break;
        }

      data++;
      length--;
      }

    bytesParsed = initialLength - length;
    return true;
    }
  //}}}
  //{{{
  void sendResponseOK (const string& filename, int fileSize) {

    string fileType;
    if (filename.find (".html") != string::npos)
      fileType = "text/html";
    else if (filename.find (".jpg") != string::npos)
      fileType = "image/jpg";
    else
      fileType = "text/plain";

    string response = format ("HTTP/1.1 200 OK\n"
                              "Server: Colin web server\n"
                              "Content-length: {}\n"
                              "Content-type: {}\n"
                              "\r\n",
                              fileSize, fileType);

    if (send (mSocket, response.c_str(), (int)response.size(), 0) < 0)
      cLog::log (LOGERROR, "sendResponseOK send failed");
    }
  //}}}
  //{{{
  void closeSocket() {

    #ifdef _WIN32
      closesocket (mSocket);
    #endif

    #ifdef __linux__
      close (mSocket);
    #endif
    }
  //}}}

  enum eState { eNone, eChar, eReturn, eLine, eDone, eError };

  const SOCKET mSocket;
  const struct sockaddr_in mSockAddrIn;
  const bool mDebug;

  eState mState = eNone;
  string mString;

  vector <string> mStrings;
  vector <string> mRequestStrings;

  //{{{
  class cHeader {
  public:
    cHeader (const string& tag, const string& value) : mTag(tag), mValue(value) {}
    ~cHeader() {}

    const string mTag;
    const string mValue;
    };
  //}}}
  vector <cHeader> mHeaders;
  };
//}}}

int main (int numArgs, char* args[]) {
  //{{{  args to params
  vector <string> params;
  for (int i = 1; i < numArgs; i++)
    params.push_back (args[i]);
  //}}}
  eLogLevel logLevel = LOGINFO;
  bool http = true;
  //{{{  parse params
  for (auto it = params.begin(); it < params.end(); ++it) {
    if (*it == "log1") { logLevel = LOGINFO1; params.erase (it); }
    else if (*it == "log2") { logLevel = LOGINFO2; params.erase (it); }
    else if (*it == "log3") { logLevel = LOGINFO3; params.erase (it); }
    else if (*it == "rtsp") { http = false; params.erase (it); }
    }
  //}}}

  cLog::init (logLevel);
  cLog::log (LOGNOTICE, "minimal http/rtsp server");

  // start server listening on well known port for clients
  cHttpServer server (http ? kHttpPortNumber : kRtspPortNumber);
  server.start();

  while (true) {
    struct sockaddr_in addr;
    SOCKET socket = server.client (addr);
    if (socket < 0) {
      cLog::log (LOGERROR, "client accept failed");
      continue;
      }

    cHttpRequest request (socket, addr, !http);
    cLog::log (LOGINFO, "accepted client " + request.getClientName() + " "  + request.getClientAddressString());
    if (request.receive()) {
      if (request.getMethod() == "GET")
        if (request.respondFile())
          continue;
      }
    request.respondNotOk();
    }
  }
