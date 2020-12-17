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
//void sendError (FILE* stream, char* cause, char* errno, char* shortmsg, char* longmsg) {
  //fprintf (stream, "HTTP/1.1 %s %s\n", errno, shortmsg);
  //fprintf (stream, "Content-type: text/html\n");
  //fprintf (stream, "\n");
  //fprintf (stream, "<html><title>Tiny Error</title>");
  //fprintf (stream, "<body bgcolor=""ffffff"">\n");
  //fprintf (stream, "%s: %s\n", errno, shortmsg);
  //fprintf (stream, "<p>%s: %s\n", longmsg, cause);
  //fprintf (stream, "<hr><em>The Tiny Web server</em>\n");
  //}
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
    char buffer[kBufferSize];
    while (true) {
      int bytes = recv (childSocket, buffer, kBufferSize, 0);
      if (bytes <= 0)
        break;
      buffer [bytes] = 0;
      cLog::log (LOGINFO, "read:%d", bytes);
      cLog::log (LOGINFO, buffer);
      }
    cLog::log (LOGINFO, "done");
    break;
    // get the HTTP request line
    //char buf[BUFSIZE];     // message buffer
    //char method[BUFSIZE];  // request method
    //char uri[BUFSIZE];     // request uri
    //char version[BUFSIZE]; // request method
    //fgets (buf, BUFSIZE, stream);
    //printf ("%s", buf);
    //sscanf (buf, "%s %s %s\n", method, uri, version);

    // tiny only supports the GET method
    //if (strcasecmp (method, "GET")) {
      //{{{  error, continue
      //cerror (stream, method, "501", "Not Implemented", "Tiny does not implement this method");
      //fclose (stream);
      //close (childSocket);
      //continue;
      //}
      //}}}

    // read (and ignore) the HTTP headers
    //fgets (buf, BUFSIZE, stream);
    //printf ("%s", buf);
    //while (strcmp (buf, "\r\n")) {
      //fgets (buf, BUFSIZE, stream);
      //printf ("%s", buf);
      //}
    //{{{  nnn
    //{{{
    //char version[BUFSIZE]; // request method
    //char filename[BUFSIZE];// path derived from uri
    //char filetype[BUFSIZE];// path derived from uri
    //char cgiargs[BUFSIZE]; // cgi argument list
    //char* p;               // temporary pointer
    //int is_static;         // static request?
    //int fd;                // static content filedes
    //int pid;               // process id from fork
    //int wait_status;       // status from wait
    //#ifdef __linux__
      //struct stat sbuf;      // file status
    //#endif
    //}}}
    //// parse the uri [crufty]
    //if (!strstr (uri, "cgi-bin")) {
      //{{{  static content
      //is_static = 1;
      //strcpy (cgiargs, "");
      //strcpy (filename, ".");
      //strcat (filename, uri);
      //if (uri[strlen(uri)-1] == '/')
        //strcat (filename, "index.html");
      //}
      //}}}
    //else {
      //{{{  dynamic content
      //is_static = 0;
      //#ifdef __linux_
      ////p = index (uri, '?');
      //if (p) {
        //strcpy (cgiargs, p+1);
        //*p = '\0';
        //}
      //else {
        //strcpy (cgiargs, "");
        //}
      //#endif
      //strcpy (filename, ".");
      //strcat (filename, uri);
      //}
      //}}}

    //// make sure the file exists
    //#ifdef __linux__
      //if (stat (filename, &sbuf) < 0) {
        //{{{  error, continue
        //cerror (stream, filename, "404", "Not found", "Tiny couldn't find this file");
        //fclose (stream);
        //close (childSocket);
        //continue;
        //}
        //}}}
    //if (is_static) {
      //{{{  serve static content
      //if (strstr (filename, ".html"))
        //strcpy (filetype, "text/html");
      //else if (strstr(filename, ".gif"))
        //strcpy(filetype, "image/gif");
      //else if (strstr(filename, ".jpg"))
        //strcpy (filetype, "image/jpg");
      //else
        //strcpy (filetype, "text/plain");

      //// print response header
      //fprintf (stream, "HTTP/1.1 200 OK\n");
      //fprintf (stream, "Server: Tiny Web Server\n");
      //fprintf (stream, "Content-length: %d\n", (int)sbuf.st_size);
      //fprintf (stream, "Content-type: %s\n", filetype);
      //fprintf (stream, "\r\n");
      //fflush (stream);

      //// Use mmap to return arbitrary-sized response body
      //fd = open (filename, O_RDONLY);
      //p = mmap (0, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
      //fwrite (p, 1, sbuf.st_size, stream);
      //munmap (p, sbuf.st_size);
      //}
      //}}}
    //else {
      //{{{  serve dynamic content
      //// make sure file is a regular executable file
      //if (!(S_IFREG & sbuf.st_mode) || !(S_IXUSR & sbuf.st_mode)) {
        //cerror (stream, filename, "403", "Forbidden", "You are not allow to access this item");
        //fclose (stream);
        //close (childSocket);
        //continue;
        //}

      //// a real server would set other CGI environ vars as well
      //setenv ("QUERY_STRING", cgiargs, 1);

      //// print first part of response header
      //sprintf (buf, "HTTP/1.1 200 OK\n");
      //write (childSocket, buf, strlen(buf));
      //sprintf (buf, "Server: Tiny Web Server\n");
      //write (childSocket, buf, strlen(buf));

      //// create and run the child CGI process so that all child
      //// output to stdout and stderr goes back to the client via the childSocket socket descriptor
      //pid = fork();
      //if (pid < 0) {
        //perror ("ERROR in fork");
        //exit(1);
        //}
      //else if (pid > 0) {
        //// parent process
        //wait (&wait_status);
        //}
      //else {
        //// child  process
        //close (0); // close stdin
        //dup2 (childSocket, 1); // map socket to stdout
        //dup2 (childfd, 2); // map socket to stderr
        ////if (execve (filename, NULL, environ) < 0) {
        ////  perror ("ERROR in execve");
        ////  }
        //}
      //}
      //}}}
    //#endif

    //// clean up
    //fclose (stream);
    //close (childSocket);
    //}}}
    }
  }
