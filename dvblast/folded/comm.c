// comm.c
//{{{  includes
#include <sys/un.h>

#include "dvblast.h"
#include "util.h"
#include "demux.h"
#include "dvb.h"
#include "comm.h"
//}}}

#define min(a, b) (a < b ? a : b)

static int iCommFd = -1;
static struct ev_io commWatcher;

//{{{
static void commRead (struct ev_loop* loop, struct ev_io* w, int revents) {

  uint8_t p_buffer[COMM_BUFFER_SIZE], p_answer[COMM_BUFFER_SIZE];
  struct sockaddr_un sun_client;
  socklen_t sun_length = sizeof(sun_client);

  ssize_t i_size = recvfrom (iCommFd, p_buffer, COMM_BUFFER_SIZE, 0, (struct sockaddr*)&sun_client, &sun_length);
  if (i_size < COMM_HEADER_SIZE ) {
    //{{{  error return
    msg_Err (NULL, "cannot read comm socket (%zd:%s)\n", i_size, strerror(errno));
    return;
    }
    //}}}
  if (sun_length == 0 || sun_length > sizeof(sun_client)) {
    //{{{  error return
    msg_Err (NULL, "anonymous packet from comm socket\n");
    return;
    }
    //}}}
  if (p_buffer[0] != COMM_HEADER_MAGIC) {
    //{{{  error return
    msg_Err (NULL, "wrong protocol version 0x%x", p_buffer[0]);
    return;
    }
    //}}}

  uint8_t* p_input = p_buffer + COMM_HEADER_SIZE;
  uint8_t* p_output = p_answer + COMM_HEADER_SIZE;
  uint8_t* p_packed_section;
  unsigned int i_packed_section_size;
  ssize_t i_answer_size = 0;
  uint8_t i_answer;

  uint8_t i_command = p_buffer[1];
  switch (i_command) {
    //{{{
    case CMD_RELOAD:
      //configReadFile();
      i_answer = RET_OK;
      i_answer_size = 0;
      break;
    //}}}
    //{{{
    case CMD_FRONTEND_STATUS:
      i_answer = dvbFrontendStatus (p_answer + COMM_HEADER_SIZE, &i_answer_size );
      break;
    //}}}
    //{{{
    case CMD_SHUTDOWN:
      ev_break (loop, EVBREAK_ALL);
      i_answer = RET_OK;
      i_answer_size = 0;
      break;
    //}}}
    case CMD_GET_PAT:
    case CMD_GET_CAT:
    case CMD_GET_NIT:
    //{{{
    case CMD_GET_SDT: {
      #define CASE_TABLE(x) \
        case CMD_GET_##x: \
          { \
            i_answer = RET_##x; \
            p_packed_section = demuxGetCurrentPacked##x(&i_packed_section_size); \
            break; \
          }

        switch (i_command) {
          CASE_TABLE(PAT)
          CASE_TABLE(CAT)
          CASE_TABLE(NIT)
          CASE_TABLE(SDT)
          }
      #undef CASE_TABLE

      if ( p_packed_section && i_packed_section_size ) {
        if ( i_packed_section_size <= COMM_BUFFER_SIZE - COMM_HEADER_SIZE ) {
          i_answer_size = i_packed_section_size;
          memcpy( p_answer + COMM_HEADER_SIZE, p_packed_section, i_packed_section_size );
          }
        else {
          msg_Err( NULL, "section size is too big (%u)\n", i_packed_section_size );
          i_answer = RET_NODATA;
          }
        free( p_packed_section );
        }
      else
        i_answer = RET_NODATA;

      break;
      }
    //}}}
    case CMD_GET_EIT_PF:
    case CMD_GET_EIT_SCHEDULE:
    //{{{
    case CMD_GET_PMT: {
      if (i_size < COMM_HEADER_SIZE + 2 ) {
        msg_Err (NULL, "command packet is too short (%zd)\n", i_size );
        return;
        }

      uint16_t i_sid = (uint16_t)((p_input[0] << 8) | p_input[1]);
      if (i_command == CMD_GET_EIT_PF ) {
        i_answer = RET_EIT_PF;
        p_packed_section = demuxGetPackedEITpf (i_sid, &i_packed_section_size);
        }
      else if (i_command == CMD_GET_EIT_SCHEDULE ) {
        i_answer = RET_EIT_SCHEDULE;
        p_packed_section = demuxGetPackedEITschedule (i_sid, &i_packed_section_size);
        }
      else {
        i_answer = RET_PMT;
        p_packed_section = demuxGetPackedPMT (i_sid, &i_packed_section_size);
        }

      if (p_packed_section && i_packed_section_size) {
        i_answer_size = i_packed_section_size;
        memcpy (p_answer + COMM_HEADER_SIZE, p_packed_section, i_packed_section_size);
        free (p_packed_section);
        }
      else {
          i_answer = RET_NODATA;
        }

      break;
      }
    //}}}
    //{{{
    case CMD_GET_PIDS: {
      i_answer = RET_PIDS;
      i_answer_size = sizeof(struct cmd_pid_info);
      demuxGetPIDSinfo( p_output );
      break;
      }
    //}}}
    //{{{
    case CMD_GET_PID: {
      if (i_size < COMM_HEADER_SIZE + 2) {
        msg_Err (NULL, "command packet is too short (%zd)\n", i_size);
        return;
        }

      uint16_t i_pid = (uint16_t)((p_input[0] << 8) | p_input[1]);
      if (i_pid >= MAX_PIDS) {
        i_answer = RET_NODATA;
        }
      else {
        i_answer = RET_PID;
        i_answer_size = sizeof(ts_pid_info_t);
        demuxGetPIDinfo (i_pid, p_output);
        }
      break;
      }
    //}}}
    //{{{
    default:
      msg_Err (NULL, "wrong command %u", i_command);
      i_answer = RET_HUH;
      i_answer_size = 0;
      break;
    //}}}
    }

  p_answer[0] = COMM_HEADER_MAGIC;
  p_answer[1] = i_answer;
  p_answer[2] = 0;
  p_answer[3] = 0;
  uint32_t* p_size = (uint32_t *)&p_answer[4];
  *p_size = i_answer_size + COMM_HEADER_SIZE;
  //* msg_Dbg( NULL, "answering %d to %d with size %zd", i_answer, i_command, i_answer_size ); */

  ssize_t i_sended = 0;
  ssize_t i_to_send = i_answer_size + COMM_HEADER_SIZE;
  do {
    ssize_t i_sent = sendto (iCommFd, p_answer + i_sended, min(i_to_send, COMM_MAX_MSG_CHUNK), 0,
                             (struct sockaddr *)&sun_client, sun_length );
    if (i_sent < 0) {
      msg_Err( NULL, "cannot send comm socket (%s)", strerror(errno) );
      break;
       }

    i_sended += i_sent;
    i_to_send -= i_sent;
    } while (i_to_send > 0);
  }
//}}}

//{{{
void commOpen() {

  unlink (psz_srv_socket);

  if ((iCommFd = socket( AF_UNIX, SOCK_DGRAM, 0 )) == -1 ) {
    msg_Err (NULL, "cannot create comm socket (%s)", strerror(errno));
    return;
    }

  int i_size = COMM_MAX_MSG_CHUNK;
  setsockopt (iCommFd, SOL_SOCKET, SO_RCVBUF, &i_size, sizeof(i_size));

  struct sockaddr_un sun_server;
  memset (&sun_server, 0, sizeof(sun_server));
  sun_server.sun_family = AF_UNIX;
  strncpy (sun_server.sun_path, psz_srv_socket, sizeof(sun_server.sun_path) );
  sun_server.sun_path[sizeof(sun_server.sun_path) - 1] = '\0';

  if (bind (iCommFd, (struct sockaddr*)&sun_server, SUN_LEN(&sun_server)) < 0) {
    msg_Err (NULL, "cannot bind comm socket (%s)", strerror(errno));
    close (iCommFd);
    iCommFd = -1;
    return;
    }

  ev_io_init (&commWatcher, commRead, iCommFd, EV_READ);
  ev_io_start (eventLoop, &commWatcher);
  }
//}}}
//{{{
void commClose() {

  if (iCommFd > -1) {
    ev_io_stop (eventLoop, &commWatcher);
    close (iCommFd);
    unlink (psz_srv_socket);
    }
  }
//}}}
