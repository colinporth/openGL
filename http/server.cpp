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

//{{{
class cHttpServer {
public:
  cHttpServer (uint16_t portNumber) : mPortNumber(portNumber) {}
  ~cHttpServer() {}

  //{{{
  void start() {

    mParentSocket = socket (AF_INET, SOCK_STREAM, 0);
    if (mParentSocket < 0)
      cLog::log (LOGERROR, "socket open failed");

    // allows us to restart server immediately
    int optval = 1;
    setsockopt (mParentSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval , sizeof(int));

    // bind port to socket
    constexpr uint16_t kPortNumber = 80;

    struct sockaddr_in serverAddr;
    memset (&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl (INADDR_ANY);
    serverAddr.sin_port = htons (kPortNumber);

    if (bind (mParentSocket, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) < 0)
      cLog::log (LOGERROR, "bind failed");

    // ready to accept connection requests
    if (listen (mParentSocket, 5) < 0) // allow 5 requests to queue up
      cLog::log (LOGERROR, "listen failed");
    }
  //}}}
  //{{{
  SOCKET accept (struct sockaddr_in& clientAddr) {

    socklen_t clientlen = sizeof (clientAddr);
    return ::accept (mParentSocket, (struct sockaddr*)&clientAddr, &clientlen);
    }
  //}}}

private:
  SOCKET mParentSocket;
  uint16_t mPortNumber;
  };
//}}}
//{{{
class cHttpRequest {
public:
  cHttpRequest (SOCKET socket) : mSocket(socket) {}
  ~cHttpRequest() {}

  string getMethod() { return mRequestStrings.size() > 0 ? mRequestStrings[0] : ""; }
  string getUri() { return mRequestStrings.size() > 1 ? mRequestStrings[1] : ""; }
  string getVersion() { return mRequestStrings.size() > 2 ? mRequestStrings[2] : ""; }
  //{{{
  bool receive() {

    constexpr int kRecvBufferSize = 128;
    uint8_t buffer[kRecvBufferSize];

    bool needMoreData = true;
    while (needMoreData) {
      auto bufferPtr = buffer;
      auto bufferBytesReceived =  recv (mSocket, (char*)buffer, kRecvBufferSize, 0);;
      if (bufferBytesReceived <= 0) {
        cLog::log (LOGERROR, "recv - no bytes %d", bufferBytesReceived);
        break;
        }
      while (needMoreData && (bufferBytesReceived > 0)) {
        int bytesReceived;
        needMoreData = parseData (bufferPtr, bufferBytesReceived, bytesReceived);
        bufferBytesReceived -= bytesReceived;
        bufferPtr += bytesReceived;
        }
      }

    if (!mLineStrings.empty()) {
      mRequestStrings = split (mLineStrings[0], ' ');
      cLog::log (LOGINFO, format ("method:{} uri:{} version:{}", getMethod(), getUri(), getVersion()));
      }

    return !mLineStrings.empty() && (mRequestStrings.size() == 3);
    }
  //}}}

  //{{{
  bool respondFile() {

    string uri = "." + getUri();

    int64_t fileSize = getFileSize (uri);
    if (fileSize) {
      sendResponseOK (uri, fileSize);

      // read file into buffer
      FILE* file = fopen (uri.c_str(), "rb");
      uint8_t* fileBuffer = (uint8_t*)malloc (fileSize);
      fread (fileBuffer, 1, fileSize, file);
      fclose (file);

      // send file from buffer
      if (send (mSocket, (const char*)fileBuffer, (int)fileSize, 0) < 0)
        cLog::log (LOGERROR, "send failed");
      free (fileBuffer);

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
                              "<hr><em>The Tiny Web server</em>\n",
                              getUri());

    if (send (mSocket, response.c_str(), (int)response.size(), 0) < 0)
      cLog::log (LOGERROR, "sendResponseNotOk send failed");

    closeSocket();
    }
  //}}}

private:
  //{{{
  static int64_t getFileSize (const string& filename) {

  #ifdef _WIN32
    struct _stati64 st;
    if (_stat64 (filename.c_str(), &st) == -1)
  #else
    struct stat st;
    if (stat (filename.c_str(), &st) == -1)
  #endif
      return 0;
    else
      return st.st_size;
    }
  //}}}
  //{{{
  static vector<string> split (const string& str, char delimiter = ' ') {

    vector <string> strings;

    size_t previous = 0;
    size_t current = str.find (delimiter);
    while (current != std::string::npos) {
      strings.push_back (str.substr (previous, current - previous));
      previous = current + 1;
      current = str.find (delimiter, previous);
      }

    strings.push_back (str.substr (previous, current - previous));

    return strings;
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
                              "Content-length: {0}\n"
                              "Content-type: {1}\n"
                              "\r\n",
                              fileSize, fileType);

    if (send (mSocket, response.c_str(), (int)response.size(), 0) < 0)
      cLog::log (LOGERROR, "sendResponseOK send failed");
    }
  //}}}
  //{{{
  bool parseData (const uint8_t* data, int length, int& bytesParsed) {

    int initialLength = length;

    while (length) {
      char ch = *data;
      switch (mLineState) {
        case eDone:
        case eNone:
        case eLine:
        case eError:
          mLineString = "";
        case eChar:
          if (ch =='\r') // return, expect newline
            mLineState = eReturn;
          else if (ch =='\n') {
            // newline before return, error
            cLog::log (LOGERROR, "newline before return");
            mLineState = eError;
            }
          else {// add to LineString
            mLineString += ch;
            mLineState = eChar;
            }
          break;

        case eReturn:
          if (ch == '\n') {
            // newline after return
            if (mLineString.empty()) {
              // empty line, done
              mLineState = eDone;
              bytesParsed = initialLength - length;
              return false;
              }
            else {
              mLineState = eLine;
              mLineStrings.push_back (mLineString);
              cLog::log (LOGINFO, mLineString);
              }
            }
          else {
            // not newline after return
            mLineState = eError;
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
  void closeSocket() {

    #ifdef _WIN32
      closesocket (mSocket);
    #endif

    #ifdef __linux__
      close (mSocket);
    #endif
    }
  //}}}

  enum eLineState { eNone, eChar, eReturn, eLine, eDone, eError };

  SOCKET mSocket;

  eLineState mLineState = eNone;
  string mLineString;

  vector <string> mLineStrings;
  vector <string> mRequestStrings;
  };
//}}}

int main (int argc, char** argv) {
  //{{{  wsa startup
  #ifdef _WIN32
    WSADATA wsaData;
    WSAStartup (MAKEWORD(2, 2), &wsaData);
  #endif
  //}}}
  cLog::init (LOGINFO);
  cLog::log (LOGNOTICE, "minimal http server");

  // start server listening on port
  cHttpServer server (80);
  server.start();

  while (true) {
    // wait for a connection request
    struct sockaddr_in clientAddr;
    SOCKET socket = server.accept (clientAddr);
    if (socket < 0) {
      cLog::log (LOGERROR, "accept failed");
      continue;
      }

    // determine who sent the message
    struct hostent* host =
      gethostbyaddr ((const char*)&clientAddr.sin_addr.s_addr, sizeof(clientAddr.sin_addr.s_addr), AF_INET);
    if (host == NULL)
      cLog::log (LOGERROR, "gethostbyaddr failed");

    char* hostAddr = inet_ntoa (clientAddr.sin_addr);
    if (hostAddr)
      cLog::log (LOGINFO, "inet_ntoa %s", hostAddr);
    else
      cLog::log (LOGERROR, "inet_ntoa failed");

    cHttpRequest request (socket);
    if (request.receive())
      if (request.getMethod() == "GET")
        if (request.respondFile())
          continue;

    request.respondNotOk();
    }
  }
