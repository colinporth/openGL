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
void error (const char *msg) {
  perror(msg);
  exit(1);
}
//}}}
//{{{
void cerror (FILE* stream, const char* cause, const char* err, const char* shortmsg, const char* longmsg) {

  fprintf (stream, "HTTP/1.1 %s %s\n", err, shortmsg);
  fprintf (stream, "Content-type: text/html\n");
  fprintf (stream, "\n");
  fprintf (stream, "<html><title>Tiny Error</title>");
  fprintf (stream, "<body bgcolor=""ffffff"">\n");
  fprintf (stream, "%s: %s\n", err, shortmsg);
  fprintf (stream, "<p>%s: %s\n", longmsg, cause);
  fprintf (stream, "<hr><em>The Tiny Web server</em>\n");
  }
//}}}

int main(int argc, char **argv) {

  //{{{  variables for connection I/O
  FILE *stream;          // stream version of childfd

  char buf[BUFSIZE];     // message buffer
  char method[BUFSIZE];  // request method
  char uri[BUFSIZE];     // request uri
  char version[BUFSIZE]; // request method

  char filename[BUFSIZE];// path derived from uri
  char filetype[BUFSIZE];// path derived from uri
  char cgiargs[BUFSIZE]; // cgi argument list

  char* p;               // temporary pointer
  int is_static;         // static request?

  struct stat sbuf;      // file status
  int fd;                // static content filedes
  int pid;               // process id from fork
  int wait_status;       // status from wait
  //}}}
  cLog::init (LOGINFO);
  cLog::log (LOGNOTICE, "http");

  // open socket descriptor
  int parentfd = socket (AF_INET, SOCK_STREAM, 0);
  if (parentfd < 0)
    cLog::log (LOGERROR, "ERROR opening socket");

  // allows us to restart server immediately
  int optval = 1;
  setsockopt (parentfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

  // bind port to socket
  int portno = 80;
  struct sockaddr_in serveraddr; // server's addr
  memset (&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl (INADDR_ANY);
  serveraddr.sin_port = htons ((unsigned short)portno);
  if (bind (parentfd, (struct sockaddr *) &serveraddr,
     sizeof(serveraddr)) < 0)
    cLog::log (LOGERROR, "ERROR on binding");

  // get us ready to accept connection requests
  if (listen (parentfd, 5) < 0) // allow 5 requests to queue up
    cLog::log (LOGERROR, "ERROR on listen");

  while (true) {
    // wait for a connection request
    struct sockaddr_in clientaddr; // client addr
    socklen_t clientlen = sizeof (clientaddr);
    int childfd = accept (parentfd, (struct sockaddr *) &clientaddr, &clientlen);
    if (childfd < 0)
      cLog::log (LOGERROR, "ERROR on accept");

    // determine who sent the message
    struct hostent* hostp = gethostbyaddr ((const char *)&clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if (hostp == NULL)
      cLog::log (LOGERROR, "ERROR on gethostbyaddr");

    char* hostaddrp = inet_ntoa (clientaddr.sin_addr);
    if (hostaddrp == NULL)
      cLog::log (LOGERROR, "ERROR on inet_ntoa\n");

    // open the child socket descriptor as a stream
    if ((stream = fdopen (childfd, "r+")) == NULL)
      cLog::log (LOGERROR, "ERROR on fdopen");

    // get the HTTP request line
    fgets (buf, BUFSIZE, stream);
    printf ("%s", buf);
    sscanf (buf, "%s %s %s\n", method, uri, version);

    // tiny only supports the GET method
    if (strcasecmp (method, "GET")) {
      cerror (stream, method, "501", "Not Implemented", "Tiny does not implement this method");
      fclose (stream);
      close (childfd);
      continue;
      }

    // read (and ignore) the HTTP headers
    fgets (buf, BUFSIZE, stream);
    printf ("%s", buf);
    while (strcmp (buf, "\r\n")) {
      fgets (buf, BUFSIZE, stream);
      printf ("%s", buf);
      }

    // parse the uri [crufty]
    if (!strstr (uri, "cgi-bin")) {
      // static content
      is_static = 1;
      strcpy (cgiargs, "");
      strcpy (filename, ".");
      strcat (filename, uri);
      if (uri[strlen(uri)-1] == '/')
        strcat (filename, "index.html");
      }
    else {
      // dynamic content
      is_static = 0;
      p = index (uri, '?');
      if (p) {
        strcpy (cgiargs, p+1);
        *p = '\0';
        }
      else {
        strcpy (cgiargs, "");
        }
      strcpy (filename, ".");
      strcat (filename, uri);
      }

    // make sure the file exists
    if (stat (filename, &sbuf) < 0) {
      cerror (stream, filename, "404", "Not found", "Tiny couldn't find this file");
      fclose (stream);
      close (childfd);
      continue;
      }

    // serve static content
    if (is_static) {
      if (strstr (filename, ".html"))
        strcpy (filetype, "text/html");
      else if (strstr(filename, ".gif"))
        strcpy (filetype, "image/gif");
      else if (strstr(filename, ".jpg"))
        strcpy (filetype, "image/jpg");
      else
        strcpy (filetype, "text/plain");

      // print response header
      fprintf (stream, "HTTP/1.1 200 OK\n");
      fprintf (stream, "Server: Tiny Web Server\n");
      fprintf (stream, "Content-length: %d\n", (int)sbuf.st_size);
      fprintf (stream, "Content-type: %s\n", filetype);
      fprintf (stream, "\r\n");
      fflush (stream);

      // Use mmap to return arbitrary-sized response body
      fd = open (filename, O_RDONLY);
      p = (char*)mmap (0, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
      fwrite (p, 1, sbuf.st_size, stream);
      munmap (p, sbuf.st_size);
      }

    // serve dynamic content
    else {
      // make sure file is a regular executable file
      if (!(S_IFREG & sbuf.st_mode) || !(S_IXUSR & sbuf.st_mode)) {
        cerror (stream, filename, "403", "Forbidden", "You are not allow to access this item");
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
      pid = fork();
      if (pid < 0) {
        cLog::log (LOGERROR, "ERROR in fork");
        exit(1);
        }
      else if (pid > 0) { // parent process
        wait (&wait_status);
        }
      else {
        // child  process
        close(0);     // close stdin
        dup2 (childfd, 1); // map socket to stdout
        dup2 (childfd, 2); // map socket to stderr
        if (execve(filename, NULL, environ) < 0) {
          cLog::log (LOGERROR, "ERROR in execve");
          }
        }
      }

    // clean up
    fclose(stream);
    close(childfd);
    }
  }
