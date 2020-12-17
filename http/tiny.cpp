//{{{  includes
#include <cstdint>
#include <string>

#ifdef _WIN32
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
  #include <sys/stat.h>
  #include <sys/mman.h>
  #include <sys/wait.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>

  #define SOCKET int
#endif

#include "../../shared/fmt/core.h"
#include "../../shared/utils/cLog.h"

using namespace std;
using namespace fmt;
//}}}

//{{{
void sendError (SOCKET socket,
                const string& cause, int errorNum,
                const string& shortmsg, const string& longmsg) {

  string reply = format ("HTTP/1.1 {0} {1}\n"
                         "Content-type: text/html\n"
                         "\n"
                         "<html><title>Tiny Error</title>"
                         "<body bgcolor=""ffffff"">\n"
                         "{0}: {1}\n"
                         "<p>{2}: {3}\n"
                         "<hr><em>The Tiny Web server</em>\n",
                         errorNum, shortmsg, longmsg, cause);

  if (send (socket, reply.c_str(), (int)reply.size(), 0) < 0)
    cLog::log (LOGERROR, "send failed");
  }
//}}}

// main
int main (int argc, char** argv) {

  //{{{  wsa startup
  #ifdef _WIN32
    WSADATA wsaData;
    WSAStartup (MAKEWORD(2, 2), &wsaData);
  #endif
  //}}}
  cLog::init (LOGINFO);
  cLog::log (LOGNOTICE, "http");

  // open socket descriptor
  SOCKET parentSocket = socket (AF_INET, SOCK_STREAM, 0);
  if (parentSocket < 0)
    cLog::log (LOGERROR, "parent socket open failed");

  // allows us to restart server immediately
  int optval = 1;
  setsockopt (parentSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval , sizeof(int));

  // bind port 80 to socket
  int portno = 80;
  struct sockaddr_in serveraddr;
  memset (&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl (INADDR_ANY);
  serveraddr.sin_port = htons ((unsigned short)portno);
  if (bind (parentSocket, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0)
    cLog::log (LOGERROR, "bind failed");

  // get us ready to accept connection requests
  if (listen (parentSocket, 5) < 0) // allow 5 requests to queue up
    cLog::log (LOGERROR, "listen failed");

  // main loop: wait for a connection request, parse HTTP, serve requested content, close connection.
  while (true) {
    // wait for a connection request
    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof (clientaddr);
    SOCKET childSocket = accept (parentSocket, (struct sockaddr*)&clientaddr, &clientlen);
    if (childSocket < 0)
      cLog::log (LOGERROR, "accept failed");

    // determine who sent the message
    struct hostent* host = gethostbyaddr (
      (const char*)&clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if (host == NULL)
      cLog::log (LOGERROR, "gethostbyaddr failed");

    char* hostaddr = inet_ntoa (clientaddr.sin_addr);
    if (hostaddr == NULL)
      cLog::log (LOGERROR, "inet_ntoa failed");

    constexpr int kBufferSize = 128;
    char buffer[kBufferSize+1];
    while (true) {
      int bytes = recv (childSocket, buffer, kBufferSize, 0);
      if (bytes <= 0)
        break;
      buffer [bytes] = 0;
      cLog::log (LOGINFO, "read:%d", bytes);
      cLog::log (LOGINFO, buffer);
      }
    sendError (childSocket, "method", 403, "forbidden", "Tiny does not implement this method");

    #ifdef _WIN32
      closesocket (childSocket);
    #endif
    #ifdef __linux__
      close (childSocket);
    #endif

    cLog::log (LOGINFO, "done");
    }
  }
