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

vector <string> mLineStrings;
vector <string> mStrings;
//{{{
void split (const string& str, char delim = ' ') {

  size_t previous = 0;
  size_t current = str.find (delim);
  while (current != std::string::npos) {
    mStrings.push_back (str.substr (previous, current - previous));
    previous = current + 1;
    current = str.find (delim, previous);
    }

  mStrings.push_back (str.substr (previous, current - previous));
  }
//}}}

//{{{
int64_t getFileSize (const string& filename) {

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
void sendResponseOK (SOCKET socket, const string& filename, int fileSize) {

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

  if (send (socket, response.c_str(), (int)response.size(), 0) < 0)
    cLog::log (LOGERROR, "sendResponseOK send failed");
  }
//}}}
//{{{
void sendFile (SOCKET socket, const string& filename, int fileSize) {

  // read file into buffer
  FILE* file = fopen (filename.c_str(), "rb");
  uint8_t* fileBuffer = (uint8_t*)malloc (fileSize);
  fread (fileBuffer, 1, fileSize, file);
  fclose (file);

  // send file from buffer
  if (send (socket, (const char*)fileBuffer, (int)fileSize, 0) < 0)
    cLog::log (LOGERROR, "send failed");
  free (fileBuffer);
  }
//}}}
//{{{
void sendResponseNotOk (SOCKET socket, const string& filename) {

  string response = format ("HTTP/1.1 404 notFound\n"
                            "Content-type: text/html\n"
                            "\n"
                            "<html><title>Tiny Error</title>"
                            "<body bgcolor=""ffffff"">\n"
                            "404: notFound\n"
                            "<p>Tiny couldn't find this file: {}\n"
                            "<hr><em>The Tiny Web server</em>\n",
                            filename);

  if (send (socket, response.c_str(), (int)response.size(), 0) < 0)
    cLog::log (LOGERROR, "sendResponseNotOk send failed");
  }
//}}}
//{{{
void closeSocket (SOCKET socket) {

  #ifdef _WIN32
    closesocket (socket);
  #endif

  #ifdef __linux__
    close (socket);
  #endif
  }
//}}}

enum eLineState { eNone, eChar, eReturn, eLine, eDone, eError };
eLineState mLineState = eNone;
bool mRequest = false;
string mLineString;
//{{{
bool parseData (const uint8_t* data, int length, int& bytesParsed) {
// GET /path  HTTP/1.1\r\n
// Host: host \r\n
// header tag: value \r\n
// \r\n;
//  method path version\r\n
//  tag value \r\n
//  \r\n

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
            cLog::log (LOGINFO, "got line - " + mLineString);
            mLineStrings.push_back (mLineString);
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

int main (int argc, char **argv) {
  //{{{  wsa startup
  #ifdef _WIN32
    WSADATA wsaData;
    WSAStartup (MAKEWORD(2, 2), &wsaData);
  #endif
  //}}}
  cLog::init (LOGINFO);
  cLog::log (LOGNOTICE, "minimal http server");

  // open socket descriptor
  SOCKET parentSocket = socket (AF_INET, SOCK_STREAM, 0);
  if (parentSocket < 0)
    cLog::log (LOGERROR, "socket open failed");

  // allows us to restart server immediately
  int optval = 1;
  setsockopt (parentSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval , sizeof(int));

  // bind port to socket
  int portno = 80;
  struct sockaddr_in serveraddr; // server's addr
  memset (&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl (INADDR_ANY);
  serveraddr.sin_port = htons ((unsigned short)portno);
  if (bind (parentSocket, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
    cLog::log (LOGERROR, "binding failed");

  // get us ready to accept connection requests
  if (listen (parentSocket, 5) < 0) // allow 5 requests to queue up
    cLog::log (LOGERROR, "listen failed");

  while (true) {
    // wait for a connection request
    struct sockaddr_in clientaddr; // client addr
    socklen_t clientlen = sizeof (clientaddr);
    SOCKET childSocket = accept (parentSocket, (struct sockaddr *) &clientaddr, &clientlen);
    if (childSocket < 0)
      cLog::log (LOGERROR, "accept failed");

    // determine who sent the message
    struct hostent* host =
      gethostbyaddr ((const char*)&clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if (host == NULL)
      cLog::log (LOGERROR, "gethostbyaddr failed");
    char* hostaddr = inet_ntoa (clientaddr.sin_addr);
    if (hostaddr == NULL)
      cLog::log (LOGERROR, "inet_ntoa");

    mLineStrings.clear();
    constexpr int kRecvBufferSize = 128;
    uint8_t buffer[kRecvBufferSize];
    bool needMoreData = true;
    while (needMoreData) {
      auto bufferPtr = buffer;
      auto bufferBytesReceived =  recv (childSocket, (char*)buffer, kRecvBufferSize, 0);;
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

    string uri;
    mStrings.clear();
    if (mLineStrings.size() > 0) {
      split (mLineStrings[0], ' ');
      if (mStrings.size() == 3) {
        cLog::log (LOGINFO, format ("method:{} uri:{} version:{}",  mStrings[0], mStrings[1], mStrings[2]));
        string method =  mStrings[0];
        uri = mStrings[1];
        if (uri  == "/")
          uri += "index.html";
        uri = "." + uri;
        string version =  mStrings[2];

        int64_t fileSize = getFileSize (uri);
        if (fileSize) {
          sendResponseOK (childSocket, uri, fileSize);
          sendFile (childSocket, uri, fileSize);
          closeSocket (childSocket);
          continue;
          }
        }
      }

    sendResponseNotOk (childSocket, uri);
    closeSocket (childSocket);
    continue;
    }
  }
