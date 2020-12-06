// udp.c
//{{{  includes
#include <net/if.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <errno.h>

#include <ev.h>

#include <bitstream/common.h>
#include <bitstream/ietf/rtp.h>

#include "dvblast.h"
#include "util.h"
#include "demux.h"
#include "udp.h"
//}}}
//{{{  defines
#define UDP_LOCK_TIMEOUT 5000000 /* 5 s */
#define PRINT_REFRACTORY_PERIOD 1000000 /* 1 s */

#define IS_OPTION(option) (!strncasecmp (psz_string, option, strlen(option)))
#define ARG_OPTION(option) (psz_string + strlen(option))
//}}}

static int i_handle;
static struct ev_io udpWatcher;
static struct ev_timer muteWatcher;

static bool b_udp = false;
static int i_block_cnt;
static uint8_t pi_ssrc[4] = { 0, 0, 0, 0 };
static uint16_t i_seqnum = 0;
static bool b_sync = false;

static mtime_t i_last_print = 0;
static struct sockaddr_storage last_addr;

//{{{
static void udpRead (struct ev_loop* loop, struct ev_io* w, int revents) {

  i_wallclock = mdate();
  if (i_last_print + PRINT_REFRACTORY_PERIOD < i_wallclock) {
    i_last_print = i_wallclock;

    struct sockaddr_storage addr;
    struct msghdr mh = {
      .msg_name = &addr,
      .msg_namelen = sizeof(addr),
      .msg_iov = NULL,
      .msg_iovlen = 0,
      .msg_control = NULL,
      .msg_controllen = 0,
      .msg_flags = 0
      };

    if (recvmsg (i_handle, &mh, MSG_DONTWAIT | MSG_PEEK) != -1 && mh.msg_namelen >= sizeof(struct sockaddr)) {
      char psz_addr[256], psz_port[42];
      if (memcmp(&addr, &last_addr, mh.msg_namelen ) &&
          getnameinfo ((const struct sockaddr *)&addr, mh.msg_namelen,
                       psz_addr, sizeof(psz_addr), psz_port, sizeof(psz_port),
                       NI_DGRAM | NI_NUMERICHOST | NI_NUMERICSERV ) == 0 ) {
        memcpy (&last_addr, &addr, mh.msg_namelen);
        msg_Info (NULL, "source: %s:%s", psz_addr, psz_port);
        }
      }
    }

  struct iovec p_iov[i_block_cnt + 1];
  block_t* p_ts, **pp_current = &p_ts;
  int i_iov, i_block;
  ssize_t i_len;
  uint8_t p_rtp_hdr[RTP_HEADER_SIZE];

  if (!b_udp) {
    // FIXME : this is wrong if RTP header > 12 bytes */
    p_iov[0].iov_base = p_rtp_hdr;
    p_iov[0].iov_len = RTP_HEADER_SIZE;
    i_iov = 1;
    }
  else
    i_iov = 0;

  for (i_block = 0; i_block < i_block_cnt; i_block++) {
    *pp_current = blockNew();
    p_iov[i_iov].iov_base = (*pp_current)->p_ts;
    p_iov[i_iov].iov_len = TS_SIZE;
    pp_current = &(*pp_current)->p_next;
    i_iov++;
    }
  pp_current = &p_ts;

  if ((i_len = readv (i_handle, p_iov, i_iov)) < 0) {
    msg_Err (NULL, "couldn't read from network (%s)", strerror(errno));
    goto err;
    }

  if (!b_udp) {
    uint8_t pi_new_ssrc[4];

    if (!rtp_check_hdr (p_rtp_hdr) )
      msg_Warn (NULL, "invalid RTP packet received");
    if (rtp_get_type (p_rtp_hdr) != RTP_TYPE_TS)
      msg_Warn (NULL, "non-TS RTP packet received");
    rtp_get_ssrc (p_rtp_hdr, pi_new_ssrc);
    if (!memcmp (pi_ssrc, pi_new_ssrc, 4 * sizeof(uint8_t))) {
      if (rtp_get_seqnum (p_rtp_hdr) != i_seqnum)
        msg_Warn (NULL, "RTP discontinuity");
      }
    else {
      struct in_addr addr;
      memcpy (&addr.s_addr, pi_new_ssrc, 4 * sizeof(uint8_t));
      msg_Dbg (NULL, "new RTP source: %s", inet_ntoa(addr));
      memcpy (pi_ssrc, pi_new_ssrc, 4 * sizeof(uint8_t));
      }
    i_seqnum = rtp_get_seqnum (p_rtp_hdr) + 1;
    i_len -= RTP_HEADER_SIZE;
    }

  i_len /= TS_SIZE;
  if (i_len) {
    if (!b_sync) {
      msg_Info (NULL, "frontend has acquired lock");
      b_sync = true;
      }

    ev_timer_again (loop, &muteWatcher);
    }

  while (i_len && *pp_current) {
    pp_current = &(*pp_current)->p_next;
    i_len--;
    }

err:
  blockDeleteChain (*pp_current);
  *pp_current = NULL;

  demuxRun (p_ts);
  }
//}}}
//{{{
static void udpMuteCb (struct ev_loop* loop, struct ev_timer* w, int revents) {

  msg_Warn (NULL, "frontend has lost lock");
  ev_timer_stop (loop, w);
  }
//}}}

// external interface
//{{{
void udpOpen() {

  // Parse configuration
  char* psz_bind;
  struct addrinfo* p_connect_ai = NULL;
  char* psz_string = strdup (psz_udp_src);
  char* psz_save = psz_string;
  if ((psz_bind = strchr( psz_string, '@' )) != NULL) {
    *psz_bind++ = '\0';
    p_connect_ai = parseNodeService (psz_string, NULL, 0);
    }
  else
    psz_bind = psz_string;

  struct addrinfo* p_bind_ai = parseNodeService (psz_bind, &psz_string, DEFAULT_PORT);
  if (p_bind_ai == NULL ) {
    msg_Err (NULL, "couldn't parse %s", psz_bind);
    exit (EXIT_FAILURE);
    }

  int i_family = p_bind_ai->ai_family;
  if (p_connect_ai != NULL && p_connect_ai->ai_family != i_family) {
    msg_Warn (NULL, "invalid connect address");
    freeaddrinfo (p_connect_ai);
    p_connect_ai = NULL;
    }

  int i_if_index = 0;
  int i_mtu = 0;
  char* psz_ifname = NULL;
  in_addr_t i_if_addr = INADDR_ANY;
  while ((psz_string = strchr (psz_string, '/')) != NULL ) {
    *psz_string++ = '\0';
    if (IS_OPTION("udp"))
      b_udp = true;
    else if (IS_OPTION("mtu="))
      i_mtu = strtol( ARG_OPTION("mtu="), NULL, 0 );
    else if (IS_OPTION("ifindex=") )
     i_if_index = strtol( ARG_OPTION("ifindex="), NULL, 0 );
    }

  if (!i_mtu)
    i_mtu = i_family == AF_INET6 ? DEFAULT_IPV6_MTU : DEFAULT_IPV4_MTU;
  i_block_cnt = (i_mtu - (b_udp ? 0 : RTP_HEADER_SIZE)) / TS_SIZE;

  // Do stuff
  if ((i_handle = socket (i_family, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    //{{{  error return
    msg_Err (NULL, "couldn't create socket (%s)", strerror(errno) );
    exit (EXIT_FAILURE);
    }
    //}}}

  int i;
  setsockopt (i_handle, SOL_SOCKET, SO_REUSEADDR, (void*)&i, sizeof(i));

  // Increase the receive buffer size to 1/2MB (8Mb/s during 1/2s) to avoid
  // packet loss caused by scheduling problems
  i = 0x80000;
  setsockopt (i_handle, SOL_SOCKET, SO_RCVBUF, (void*)&i, sizeof(i));

  if (bind (i_handle, p_bind_ai->ai_addr, p_bind_ai->ai_addrlen) < 0) {
    msg_Err (NULL, "couldn't bind (%s)", strerror(errno) );
    close (i_handle);
    exit (EXIT_FAILURE);
    }

  if (p_connect_ai != NULL) {
    uint16_t i_port;
    if (i_family == AF_INET6)
      i_port = ((struct sockaddr_in6*)p_connect_ai->ai_addr)->sin6_port;
    else
      i_port = ((struct sockaddr_in*)p_connect_ai->ai_addr)->sin_port;

    if (i_port != 0 && connect (i_handle, p_connect_ai->ai_addr, p_connect_ai->ai_addrlen) < 0)
      msg_Warn( NULL, "couldn't connect socket (%s)", strerror(errno) );
    }

  // Join the multicast group if the socket is a multicast address
  if (i_family == AF_INET6) {
    struct sockaddr_in6* p_addr = (struct sockaddr_in6 *)p_bind_ai->ai_addr;
    if ( IN6_IS_ADDR_MULTICAST( &p_addr->sin6_addr)) {
      struct ipv6_mreq imr;
      imr.ipv6mr_multiaddr = p_addr->sin6_addr;
      imr.ipv6mr_interface = i_if_index;
      if (i_if_addr != INADDR_ANY )
        msg_Warn (NULL, "ignoring ifaddr option in IPv6");

      if (setsockopt (i_handle, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char *)&imr, sizeof(struct ipv6_mreq)) < 0)
        msg_Warn (NULL, "couldn't join multicast group (%s)", strerror(errno));
      }
    }
  else {
    struct sockaddr_in* p_addr = (struct sockaddr_in*)p_bind_ai->ai_addr;
    if (IN_MULTICAST(ntohl (p_addr->sin_addr.s_addr))) {
      if  (p_connect_ai != NULL) {
        #ifndef IP_ADD_SOURCE_MEMBERSHIP
          msg_Err (NULL, "IP_ADD_SOURCE_MEMBERSHIP is unsupported");
        #else
          /* Source-specific multicast */
          struct sockaddr* p_src = p_connect_ai->ai_addr;
          struct ip_mreq_source imr;
          imr.imr_multiaddr = p_addr->sin_addr;
          imr.imr_interface.s_addr = i_if_addr;
          imr.imr_sourceaddr = ((struct sockaddr_in *)p_src)->sin_addr;
          if (i_if_index)
            msg_Warn (NULL, "ignoring ifindex option in SSM");

          if (setsockopt (i_handle, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, (char*)&imr, sizeof(struct ip_mreq_source)) < 0)
            msg_Warn (NULL, "couldn't join multicast group (%s)", strerror(errno));
        #endif
          }
        else if (i_if_index) {
          // Linux-specific interface-bound multicast */
          struct ip_mreqn imr;
          imr.imr_multiaddr = p_addr->sin_addr;
          #if defined(__linux__)
            imr.imr_address.s_addr = i_if_addr;
            imr.imr_ifindex = i_if_index;
          #endif

          if (setsockopt (i_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&imr, sizeof(struct ip_mreqn)) < 0)
            msg_Warn (NULL, "couldn't join multicast group (%s)", strerror(errno));
          }
        else {
          // Regular multicast */
          struct ip_mreq imr;
          imr.imr_multiaddr = p_addr->sin_addr;
          imr.imr_interface.s_addr = i_if_addr;
          if (setsockopt (i_handle, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&imr, sizeof(struct ip_mreq)) == -1)
            msg_Warn (NULL, "couldn't join multicast group (%s)", strerror(errno));
          }
      #ifdef SO_BINDTODEVICE
        if (psz_ifname) {
          if (setsockopt (i_handle, SOL_SOCKET, SO_BINDTODEVICE, psz_ifname, strlen(psz_ifname)+1) < 0) {
            msg_Err (NULL, "couldn't bind to device %s (%s)", psz_ifname, strerror(errno) );
            }
          free (psz_ifname);
          psz_ifname = NULL;
          }
      #endif
      }
    }

  freeaddrinfo (p_bind_ai);
  if (p_connect_ai != NULL)
    freeaddrinfo (p_connect_ai);
  free (psz_save);

  msg_Dbg (NULL, "binding socket to %s", psz_udp_src );

  ev_io_init (&udpWatcher, udpRead, i_handle, EV_READ);
  ev_io_start (eventLoop, &udpWatcher);

  ev_timer_init (&muteWatcher, udpMuteCb, UDP_LOCK_TIMEOUT / 1000000., UDP_LOCK_TIMEOUT / 1000000.);
  memset (&last_addr, 0, sizeof(last_addr));
  }
//}}}

int udpSetFilter (uint16_t i_pid ) { return -1; }
void udpUnsetFilter (int i_fd, uint16_t i_pid ) {}
void udpReset() {}
