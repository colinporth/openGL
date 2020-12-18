//{{{
// tiny.c - a minimal HTTP server that serves static and
// *          dynamic content with the GET method. Neither
// *          robust, secure, nor modular. Use for instructional
// *          purposes only.
// *          Dave O'Hallaron, Carnegie Mellon
//}}}
//{{{  includes
#include <cstdint>
#include <string>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 1024
#define MAXERRS 16

#include "../../shared/fmt/core.h"
#include "../../shared/utils/cLog.h"

using namespace std;
using namespace fmt;
//}}}

extern char** environ; // the environment

//{{{
void reply (int socket, const char* cause, const char* err, const char* shortmsg, const char* longmsg) {

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
    cLog::log (LOGERROR, "send failed");
  }
//}}}

int main(int argc, char **argv) {

  cLog::init (LOGINFO);
  cLog::log (LOGNOTICE, "http");

  //{{{  open socket descriptor
  int parentfd = socket (AF_INET, SOCK_STREAM, 0);
  if (parentfd < 0)
    cLog::log (LOGERROR, "socket open failed");
  //}}}
  //{{{  allows us to restart server immediately
  int optval = 1;
  setsockopt (parentfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval , sizeof(int));
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
    int childfd = accept (parentfd, (struct sockaddr *) &clientaddr, &clientlen);
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
    //{{{  open child socket descriptor as a stream and parse request, headers
    FILE* stream = fdopen (childfd, "r+");
    if (stream == NULL)
      cLog::log (LOGERROR, "fdopen failed");

    // get the HTTP request line
    char buf[BUFSIZE];     // message buffer
    char method[BUFSIZE];  // request method
    char uri[BUFSIZE];     // request uri
    char version[BUFSIZE]; // request method

    fgets (buf, BUFSIZE, stream);
    sscanf (buf, "%s %s %s\n", method, uri, version);
    cLog::log (LOGINFO, format ("{} method:{} uri:{} version:{}", buf, method, uri, version));

    // only support GET method
    if (strcasecmp (method, "GET")) {
      reply (childfd, method, "501", "Not Implemented", "Tiny does not implement this method");
      fclose (stream);
      close (childfd);
      continue;
      }

    // read and ignore the HTTP headers
    fgets (buf, BUFSIZE, stream);
    printf ("%s", buf);
    while (strcmp (buf, "\r\n")) {
      fgets (buf, BUFSIZE, stream);
      cLog::log (LOGINFO, "%s", buf);
      }
    //}}}

    bool isStatic;
    char filename[BUFSIZE];
    char filetype[BUFSIZE];
    char cgiargs[BUFSIZE];
    if (!strstr (uri, "cgi-bin")) {
      //{{{  static content
      isStatic = true;

      strcpy (cgiargs, "");
      strcpy (filename, ".");
      strcat (filename, uri);

      if (uri[strlen(uri)-1] == '/')
        strcat (filename, "index.html");
      }
      //}}}
    else {
      //{{{  dynamic content
      isStatic = false;

      char* p = index (uri, '?');
      if (p) {
        strcpy (cgiargs, p+1);
        *p = '\0';
        }
      else
        strcpy (cgiargs, "");

      strcpy (filename, ".");
      strcat (filename, uri);
      }
      //}}}

    // make sure the file exists
    struct stat sbuf;  
    if (stat (filename, &sbuf) < 0) {
      //{{{  no file
      reply (childfd, filename, "404", "Not found", "Tiny couldn't find this file");
      fclose (stream);
      close (childfd);
      continue;
      }
      //}}}
    if (isStatic) {
      //{{{  serve static content
      if (strstr (filename, ".html"))
        strcpy (filetype, "text/html");
      else if (strstr(filename, ".gif"))
        strcpy (filetype, "image/gif");
      else if (strstr(filename, ".jpg"))
        strcpy (filetype, "image/jpg");
      else
        strcpy (filetype, "text/plain");

      // print response header
      string reply = format ("HTTP/1.1 200 OK\n"
                             "Server: Tiny Web Server\n"
                             "Content-length: {}\n"
                             "Content-type: {}\n"
                             "\r\n",
                             (int)sbuf.st_size, filetype);

      cLog::log (LOGINFO, reply);
      if (send (childfd, reply.c_str(), (int)reply.size(), 0) < 0)
        cLog::log (LOGERROR, "send failed");

      // Use mmap to return arbitrary-sized response body
      int fd = open (filename, O_RDONLY);
      char* p = (char*)mmap (0, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
      fwrite (p, 1, sbuf.st_size, stream);
      munmap (p, sbuf.st_size);
      cLog::log (LOGINFO, "file sent %s", filename);
      }
      //}}}
    else {
      //{{{  serve dynamic content
      // make sure file is a regular executable file
      if (!(S_IFREG & sbuf.st_mode) || !(S_IXUSR & sbuf.st_mode)) {
        reply (childfd, filename, "403", "Forbidden", "You are not allow to access this item");
        fclose (stream);
        close (childfd);
        continue;
        }

      // a real server would set other CGI environ vars as well
      setenv ("QUERY_STRING", cgiargs, 1);

      // print first part of response header
      sprintf (buf, "HTTP/1.1 200 OK\n");
      write (childfd, buf, strlen(buf));
      sprintf (buf, "Server: Tiny Web Server\n");
      write (childfd, buf, strlen(buf));

      // create and run the child CGI process so that all child
      // output to stdout and stderr goes back to the client via the
      //childfd socket descriptor
      int pid = fork();
      if (pid < 0) {
        cLog::log (LOGERROR, "ERROR in fork");
        exit(1);
        }
      else if (pid > 0) { // parent process
        int waitStatus;
        wait (&waitStatus);
        }
      else {
        // child  process
        close(0);     // close stdin
        dup2 (childfd, 1); // map socket to stdout
        dup2 (childfd, 2); // map socket to stderr
        if (execve(filename, NULL, environ) < 0) {
          cLog::log (LOGERROR, "execve  failed");
          }
        }
      }
      //}}}

    // clean up
    fclose (stream);
    close (childfd);
    }
  }
