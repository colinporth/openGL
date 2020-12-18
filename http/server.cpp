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
string getFileType (const string& filename) {

  if (filename.find (".html") != string::npos)
    return "text/html";
  else if (filename.find (".jpg") != string::npos)
    return "image/jpg";
  else
    return "text/plain";
  }
//}}}

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
void response (SOCKET socket, const char* cause, const char* err, const char* shortmsg, const char* longmsg) {

  string reply = format ("HTTP/1.1 {0} {1}\n"
                         "Content-type: text/html\n"
                         "\n"
                         "<html><title>Tiny Error</title>"
                         "<body bgcolor=""ffffff"">\n"
                         "{0}: {1}\n"
                         "<p>{2}: {3}\n"
                         "<hr><em>The Tiny Web server</em>\n",
                         err, shortmsg, longmsg, cause);

  if (send (socket, reply.c_str(), (int)reply.size(), 0) < 0)
    cLog::log (LOGERROR, "response send failed");
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
//{{{  could use tiny http parser
//{{{
//enum eHeaderState {
  //eHeaderDone,
  //eHeaderContinue,
  //eHeaderVersionCharacter,
  //eHeaderCodeCharacter,
  //eHeaderStatusCharacter,
  //eHeaderKeyCharacter,
  //eHeaderValueCharacter,
  //eHeaderStoreKeyValue,
  //};
//}}}
//{{{
//const uint8_t kHeaderState[88] = {
////  *    \t    \n   \r    ' '     ,     :   PAD
  //0x80,    1, 0xC1, 0xC1,    1, 0x80, 0x80, 0xC1,  // state 0:  HTTP version
  //0x81,    2, 0xC1, 0xC1,    2,    1,    1, 0xC1,  // state 1:  Response code
  //0x82, 0x82,    4,    3, 0x82, 0x82, 0x82, 0xC1,  // state 2:  Response reason
  //0xC1, 0xC1,    4, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1,  // state 3:  HTTP version newline
  //0x84, 0xC1, 0xC0,    5, 0xC1, 0xC1,    6, 0xC1,  // state 4:  Start of header field
  //0xC1, 0xC1, 0xC0, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1,  // state 5:  Last CR before end of header
  //0x87,    6, 0xC1, 0xC1,    6, 0x87, 0x87, 0xC1,  // state 6:  leading whitespace before header value
  //0x87, 0x87, 0xC4,   10, 0x87, 0x88, 0x87, 0xC1,  // state 7:  header field value
  //0x87, 0x88,    6,    9, 0x88, 0x88, 0x87, 0xC1,  // state 8:  Split value field value
  //0xC1, 0xC1,    6, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1,  // state 9:  CR after split value field
  //0xC1, 0xC1, 0xC4, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1,  // state 10: CR after header value
  //};
//}}}
//{{{
//eHeaderState parseHeaderChar (char ch) {
//// Parses a single character of an HTTP header stream. The state parameter is
//// used as internal state and should be initialized to zero for the first call.
//// Return value is a value from the http_header enuemeration specifying
//// the semantics of the character. If an error is encountered,
//// http_header_done will be returned with a non-zero state parameter. On
//// success http_header_done is returned with the state parameter set to zero.

  //auto code = 0;
  //switch (ch) {
    //case '\t': code = 1; break;
    //case '\n': code = 2; break;
    //case '\r': code = 3; break;
    //case  ' ': code = 4; break;
    //case  ',': code = 5; break;
    //case  ':': code = 6; break;
    //}

  //auto newstate = kHeaderState [mHeaderState * 8 + code];
  //mHeaderState = (eHeaderState)(newstate & 0xF);

  //switch (newstate) {
    //case 0xC0: return eHeaderDone;
    //case 0xC1: return eHeaderDone;
    //case 0xC4: return eHeaderStoreKeyValue;
    //case 0x80: return eHeaderVersionCharacter;
    //case 0x81: return eHeaderCodeCharacter;
    //case 0x82: return eHeaderStatusCharacter;
    //case 0x84: return eHeaderKeyCharacter;
    //case 0x87: return eHeaderValueCharacter;
    //case 0x88: return eHeaderValueCharacter;
    //}

  //return eHeaderContinue;
  //}
//}}}
//{{{
//bool parseData (const uint8_t* data, int length, int& bytesParsed) {
//{{{  get desription
//// GET /path  HTTP/1.1\r\n
//// Host: host \r\n
//// header tag: value \r\n
//// \r\n;

////  method path version\r\n
////  tag value \r\n
////  \r\n
//}}}

  //int initialLength = length;

  //while (length) {
    //switch (parseHeaderChar (*data)) {
      //case eHeaderCodeCharacter: // response char
        //mResponse = mResponse * 10 + *data - '0';
        //break;

      //case eHeaderKeyCharacter: // key char
        //if (mKeyLen >= mHeaderBufferAllocSize) {
          //mHeaderBufferAllocSize *= 2;
          //mHeaderBuffer = (char*)realloc (mHeaderBuffer, mHeaderBufferAllocSize);
          //cLog::log (LOGINFO, "mHeaderBuffer key realloc %d %d", mKeyLen, mHeaderBufferAllocSize);
          //}
        //mHeaderBuffer [mKeyLen] = tolower (*data);
        //mKeyLen++;
        //break;

      //case eHeaderValueCharacter: // value char
        //if (mKeyLen + mValueLen >= mHeaderBufferAllocSize) {
          //mHeaderBufferAllocSize *= 2;
          //mHeaderBuffer = (char*)realloc (mHeaderBuffer, mHeaderBufferAllocSize);
          //cLog::log (LOGINFO, "mHeaderBuffer value realloc %d %d", mKeyLen + mValueLen, mHeaderBufferAllocSize);
          //}
        //mHeaderBuffer [mKeyLen + mValueLen] = *data;
        //mValueLen++;
        //break;

      //case eHeaderStoreKeyValue: { // key value
        //string key = string (mHeaderBuffer, size_t (mKeyLen));
        //string value = string (mHeaderBuffer + mKeyLen, size_t (mValueLen));
        //if (key == "content-length") {
          //mHeaderContentLength = stoi (value);
          //mContent = (uint8_t*)malloc (mHeaderContentLength);
          //mContentLengthLeft = mHeaderContentLength;
          //mContentState = eContentLength;
          //}
        //else if (key == "transfer-encoding")
          //mContentState = value == "chunked" ? eContentChunked : eContentNone;
        //else if (key == "location")
          //mRedirectUrl.parse (value);
        //mKeyLen = 0;
        //mValueLen = 0;
        //headerCallback (key, value);
        //break;
        //}

      //case eHeaderDone: // done
        //if (mHeaderState != eHeaderDone)
          //mState = eError;
        //else if (mContentState == eContentChunked) {
          //mState = eChunkHeader;
          //mHeaderContentLength = 0;
          //mContentLengthLeft = 0;
          //mContentReceivedSize = 0;
          //}
        //else if (mContentState == eContentNone)
          //mState = eStreamData;
        //else if (mHeaderContentLength > 0)
          //mState = eExpectedData;
        //else if (mHeaderContentLength == 0)
          //mState = eClose;
        //else
          //mState = eError;
        //break;

      //default:;
      //}

    //data++;
    //length--;
    //}

  //if ((mState == eError) || (mState == eClose)) {
    //bytesParsed = initialLength - length;
    //return false;
    //}

  //bytesParsed = initialLength - length;
  //return true;
  //}
//}}}
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
  cLog::log (LOGNOTICE, "http");

  //{{{  open socket descriptor
  SOCKET parentfd = socket (AF_INET, SOCK_STREAM, 0);
  if (parentfd < 0)
    cLog::log (LOGERROR, "socket open failed");
  //}}}
  //{{{  allows us to restart server immediately
  int optval = 1;
  setsockopt (parentfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval , sizeof(int));
  //}}}
  //{{{  bind port to socket
  int portno = 80;
  struct sockaddr_in serveraddr; // server's addr
  memset (&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl (INADDR_ANY);
  serveraddr.sin_port = htons ((unsigned short)portno);
  if (bind (parentfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
    cLog::log (LOGERROR, "binding failed");
  //}}}
  //{{{  get us ready to accept connection requests
  if (listen (parentfd, 5) < 0) // allow 5 requests to queue up
    cLog::log (LOGERROR, "listen failed");
  //}}}

  while (true) {
    //{{{  wait for a connection request
    struct sockaddr_in clientaddr; // client addr
    socklen_t clientlen = sizeof (clientaddr);
    SOCKET childfd = accept (parentfd, (struct sockaddr *) &clientaddr, &clientlen);
    if (childfd < 0)
      cLog::log (LOGERROR, "accept failed");
    //}}}
    //{{{  determine who sent the message
    struct hostent* host =
      gethostbyaddr ((const char*)&clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if (host == NULL)
      cLog::log (LOGERROR, "gethostbyaddr failed");

    char* hostaddr = inet_ntoa (clientaddr.sin_addr);
    if (hostaddr == NULL)
      cLog::log (LOGERROR, "inet_ntoa");
    //}}}

    string uri;
    mLineStrings.clear();
    mStrings.clear();

    constexpr int kRecvBufferSize = 128;
    uint8_t buffer[kRecvBufferSize];
    bool needMoreData = true;
    while (needMoreData) {
      auto bufferPtr = buffer;
      auto bufferBytesReceived =  recv (childfd, (char*)buffer, kRecvBufferSize, 0);;
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
          // file exists, send OK response, Content-length, Content-type
          string response = format ("HTTP/1.1 200 OK\n"
                                    "Server: Tiny Web Server\n"
                                    "Content-length: {}\n"
                                    "Content-type: {}\n" "\r\n",
                                    fileSize, getFileType (uri));
          cLog::log (LOGINFO, response);
          if (send (childfd, response.c_str(), (int)response.size(), 0) < 0)
            cLog::log (LOGERROR, "send failed");

          // read file into buffer
          FILE* file = fopen (uri.c_str(), "rb");
          uint8_t* fileBuffer = (uint8_t*)malloc (fileSize);
          fread (fileBuffer, 1, fileSize, file);
          fclose (file);

          // send file from buffer
          if (send (childfd, (const char*)fileBuffer, (int)fileSize, 0) < 0)
            cLog::log (LOGERROR, "send failed");
          free (fileBuffer);

          cLog::log (LOGINFO, format ("{} {}:bytes sent", uri, fileSize));

          // clean up
          closeSocket (childfd);
          continue;
          }
        }
      }

    response (childfd, uri.c_str(), "404", "Not found", "Tiny couldn't find this file");
    closeSocket (childfd);
    continue;
    }
  }
