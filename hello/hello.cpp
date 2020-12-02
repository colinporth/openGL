//{{{  includes
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <stdio.h>

// Link with ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")
//}}}

int rtp() {

  WSADATA wsaData;
  int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (result != NO_ERROR) {
    printf("WSAStartup failed with error %d\n", result);
    return 1;
    }

  // Create a receiver socket to receive datagrams
  SOCKET recvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (recvSocket == INVALID_SOCKET) {
    printf ("socket failed with error %d\n", WSAGetLastError());
    return 1;
    }

  // Bind the socket to any address and the specified port.
  unsigned short Port = 5002;
  struct sockaddr_in recvAddr;
  recvAddr.sin_family = AF_INET;
  recvAddr.sin_port = htons (Port);
  recvAddr.sin_addr.s_addr = htonl (INADDR_ANY);
  result = bind (recvSocket, (SOCKADDR*)&recvAddr, sizeof(recvAddr));
  if (result != 0) {
    printf ("bind failed with error %d\n", WSAGetLastError());
    return 1;
    }

  printf ("Receiving datagrams...\n");
  while (true) {
    char recvBuf[2048];
    int bufLen = 2048;
    struct sockaddr_in senderAddr;
    int senderAddrSize = sizeof (senderAddr);
    result = recvfrom (recvSocket, recvBuf, bufLen, 0, (SOCKADDR*)&senderAddr, &senderAddrSize);
    if (result == SOCKET_ERROR)
      printf ("recvfrom failed with error %d\n", WSAGetLastError());
    else
      printf ("recvfrom ok with %d %d %d\n", WSAGetLastError(), bufLen, result);
    }

  // Close the socket when finished receiving datagrams
  printf ("Finished receiving. Closing socket.\n");
  result = closesocket (recvSocket);
  if (result == SOCKET_ERROR) {
    printf ("closesocket failed with error %d\n", WSAGetLastError());
    return 1;
    }

  // Clean up and exit.
  printf ("Exiting.\n");

  WSACleanup();
  }

int main() {
  return rtp();
  }
