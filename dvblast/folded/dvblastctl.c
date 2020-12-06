// dvbblastctl.c - based on videolan
//{{{  includes
#include <sys/param.h>
#include <sys/un.h>
#include <errno.h>
#include <getopt.h>
#include <iconv.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>
#include <bitstream/dvb/si_print.h>
#include <bitstream/mpeg/psi_print.h>

#include "dvblast.h"
#include "util.h"

#include <stddef.h>
#define STRINGIFY( z )   UGLY_KLUDGE( z )
#define UGLY_KLUDGE( z ) #z
#include "comm.h"
//}}}

//{{{
struct dvblastctl_option {
  char*      opt;
  int        nparams;
  ctl_cmd_t  cmd;
  };
//}}}

int i_verbose = 3;
int i_syslog = 0;
unsigned int i_timeout_seconds = 15;

mtime_t now;

int i_fd = -1;
char psz_client_socket[PATH_MAX] = {0};
static iconv_t iconv_handle = (iconv_t)-1;

//{{{
static void clean_client_socket() {

  if ( i_fd > -1 ) {
    close( i_fd );
    i_fd = -1;
    }

  if ( psz_client_socket[0] ) {
    unlink( psz_client_socket );
    psz_client_socket[0] = '\0';
    }
  }
//}}}
//{{{
uint8_t** psiUnpackSections (uint8_t* p_flat_sections, unsigned int i_size ) {

  uint8_t** pp_sections;
  unsigned int i, i_offset = 0;

  pp_sections = psi_table_allocate();
  if (!pp_sections) {
    msg_Err (NULL, "%s: cannot allocate PSI table\n", __func__);
    return NULL;
    }

  psi_table_init (pp_sections);

  for (i = 0; i < PSI_TABLE_MAX_SECTIONS; i++) {
    uint8_t* p_section = p_flat_sections + i_offset;
    uint16_t i_section_len = psi_get_length (p_section) + PSI_HEADER_SIZE;

    if (!psi_validate (p_section)) {
      msg_Err (NULL, "%s: Invalid section %d\n", __func__, i);
      psi_table_free (pp_sections);
      return NULL;
      }

    // Must use allocated section not p_flat_section + offset directly! */
    uint8_t* p_section_local = psi_private_allocate();
    if (!p_section_local) {
      msg_Err (NULL, "%s: cannot allocate PSI private\n", __func__);
      psi_table_free (pp_sections);
      return NULL;
      }
    memcpy (p_section_local, p_section, i_section_len);

    // We ignore the return value of psi_table_section(), because it is useless
    // in this case. We are building the table section by section and when we have
    // more than one section in a table, psi_table_section() returns false when section 0 is added
   psi_table_section (pp_sections, p_section_local);

    i_offset += i_section_len;
    if (i_offset >= i_size - 1)
      break;
    }

  return pp_sections;
  }
//}}}

//{{{
/*****************************************************************************
 * The following two functions are from biTStream's examples and are under the
 * WTFPL (see LICENSE.WTFPL).
 ****************************************************************************/
__attribute__ ((format(printf, 2, 3))) static void psi_print(void* _unused, const char* psz_format, ...) {

  char psz_fmt[strlen(psz_format) + 2];

  va_list args;
  va_start(args, psz_format);

  strcpy(psz_fmt, psz_format);
  strcat(psz_fmt, "\n");

  vprintf(psz_fmt, args);
  va_end(args);
  }
//}}}
//{{{
__attribute__ ((format(printf, 1, 2))) void return_error( const char* psz_format, ... ) {

  va_list args;
  char psz_fmt[1024];

  clean_client_socket();

  va_start( args, psz_format );
  snprintf( psz_fmt, sizeof(psz_fmt) - 1, "ERROR: %s\n", psz_format );

  psz_fmt[sizeof(psz_fmt) - 1] = '\0';
  vfprintf( stderr, psz_fmt, args );
  va_end(args);

  exit(255);
  }
//}}}

//{{{
static char* iconv_append_null (const char* p_string, size_t i_length) {

  char *psz_string = malloc(i_length + 1);
  memcpy(psz_string, p_string, i_length);
  psz_string[i_length] = '\0';
  return psz_string;
  }
//}}}

const char* psz_native_charset = "UTF-8//IGNORE";
//{{{
char* psi_iconv (void *_unused, const char* psz_encoding, char* p_string, size_t i_length) {

  static const char *psz_current_encoding = "";

  char *psz_string, *p;
  size_t i_out_length;

  if (!strcmp(psz_encoding, psz_native_charset))
    return iconv_append_null(p_string, i_length);

  if (iconv_handle != (iconv_t)-1 &&
    strcmp(psz_encoding, psz_current_encoding)) {
    iconv_close(iconv_handle);
    iconv_handle = (iconv_t)-1;
    }

  if (iconv_handle == (iconv_t)-1)
    iconv_handle = iconv_open(psz_native_charset, psz_encoding);
  if (iconv_handle == (iconv_t)-1) {
    msg_Warn(NULL, "couldn't open converter from %s to %s (%m)", psz_encoding, psz_native_charset);
    return iconv_append_null(p_string, i_length);
    }
  psz_current_encoding = psz_encoding;

  /* converted strings can be up to six times larger */
  i_out_length = i_length * 6;
  p = psz_string = malloc(i_out_length);
  if (iconv(iconv_handle, &p_string, &i_length, &p, &i_out_length) == -1) {
    msg_Warn (NULL, "couldn't convert from %s to %s (%m)", psz_encoding, psz_native_charset);
    free (psz_string);
    return iconv_append_null(p_string, i_length);
    }

  if (i_length)
    msg_Warn (NULL, "partial conversion from %s to %s", psz_encoding, psz_native_charset);

  *p = '\0';
  return psz_string;
  }
//}}}

void print_pids_header() { }
void print_pids_footer() { }
//{{{
void print_pid (uint16_t i_pid, ts_pid_info_t* p_info) {

  if ( p_info->i_packets == 0 )
    return;

  printf ("pid %d packn %lu ccerr %lu tserr %lu Bps %lu seen %"PRId64"\n",
          i_pid,
          p_info->i_packets,
          p_info->i_cc_errors,
          p_info->i_transport_errors,
          p_info->i_bytes_per_sec,
          now - p_info->i_last_packet_ts);
  }
//}}}
//{{{
void print_pids (uint8_t* p_data) {

  int i_pid;
  print_pids_header();
  for ( i_pid = 0; i_pid < MAX_PIDS; i_pid++ ) {
    ts_pid_info_t *p_info = (ts_pid_info_t *)(p_data + i_pid * sizeof(ts_pid_info_t));
    print_pid( i_pid, p_info );
    }

  print_pids_footer();
  }
//}}}
//{{{
void print_eit_events (uint8_t* p_eit, f_print pf_print, void* print_opaque,
                       f_iconv pf_iconv, void *iconv_opaque, print_type_t i_print_type) {

  uint8_t* p_event;
  uint8_t j = 0;
  while ((p_event = eit_get_event(p_eit, j)) != NULL) {
    j++;
    char start_str[24], duration_str[12];
    int duration, hour, min, sec;
    time_t start_ts;

    start_ts = dvb_time_format_UTC (eitn_get_start_time(p_event), NULL, start_str);

    dvb_time_decode_bcd (eitn_get_duration_bcd(p_event), &duration, &hour, &min, &sec);
    sprintf (duration_str, "%02d:%02d:%02d", hour, min, sec);

    pf_print (print_opaque, "  * EVENT id=%u start_time=%ld start_time_dec=\"%s\" duration=%u duration_dec=%s running=%d free_CA=%d",
              eitn_get_event_id(p_event),
              start_ts, start_str,
              duration, duration_str,
              eitn_get_running(p_event),
              eitn_get_ca(p_event)
              );

    descs_print (eitn_get_descs(p_event), pf_print, print_opaque, pf_iconv, iconv_opaque, PRINT_TEXT);
    }
  }
//}}}
//{{{
void print_eit (uint8_t* p_eit, unsigned int i_eit_size, f_print pf_print,
                void *print_opaque, f_iconv pf_iconv, void *iconv_opaque, print_type_t i_print_type) {

  uint8_t* p_eit_end = p_eit + i_eit_size;
  uint8_t i_tid = psi_get_tableid(p_eit);
  const char *psz_tid = "unknown";

  if (i_tid == EIT_TABLE_ID_PF_ACTUAL)
    psz_tid = "actual_pf";
  else if (i_tid == EIT_TABLE_ID_PF_OTHER)
    psz_tid = "other_pf";
  else if (i_tid >= EIT_TABLE_ID_SCHED_ACTUAL_FIRST && i_tid <= EIT_TABLE_ID_SCHED_ACTUAL_LAST)
    psz_tid = "actual_schedule";
  else if (i_tid >= EIT_TABLE_ID_SCHED_OTHER_FIRST && i_tid <= EIT_TABLE_ID_SCHED_OTHER_LAST)
    psz_tid = "other_schedule";

  pf_print (print_opaque,
            "new EIT tableid=0x%02x type=%s service_id=%u version=%u%s"
            " tsid=%u"
            " onid=%u",
            i_tid, psz_tid,
            eit_get_sid(p_eit),
            psi_get_version(p_eit),
            !psi_get_current(p_eit) ? " (next)" : "",
            eit_get_tsid(p_eit),
            eit_get_onid(p_eit)
            );

  while (p_eit < p_eit_end) {
    print_eit_events(p_eit, pf_print, print_opaque, pf_iconv, iconv_opaque, i_print_type);
    p_eit += psi_get_length( p_eit ) + PSI_HEADER_SIZE;
    }

  pf_print(print_opaque, "end EIT");
  }
//}}}

//{{{
static const struct dvblastctl_option options[] = {
  { "reload",             0, CMD_RELOAD },
  { "shutdown",           0, CMD_SHUTDOWN },
  { "fe_status",          0, CMD_FRONTEND_STATUS },
  { "get_pat",            0, CMD_GET_PAT },
  { "get_cat",            0, CMD_GET_CAT },
  { "get_nit",            0, CMD_GET_NIT },
  { "get_sdt",            0, CMD_GET_SDT },
  { "get_eit_pf",         1, CMD_GET_EIT_PF }, /* arg: service_id (uint16_t) */
  { "get_eit_schedule",   1, CMD_GET_EIT_SCHEDULE }, /* arg: service_id (uint16_t) */
  { "get_pmt",            1, CMD_GET_PMT }, /* arg: service_id (uint16_t) */
  { "get_pids",           0, CMD_GET_PIDS },
  { "get_pid",            1, CMD_GET_PID },  /* arg: pid (uint16_t) */
  { NULL, 0, 0 }
  };
//}}}
//{{{
void usage() {

  printf("DVBlastctl %s (%s)\n", VERSION, VERSION_EXTRA );
  printf("Usage: dvblastctl -r <remote socket> [-x <text|xml>] [cmd]\n");
  printf("Options:\n");
  printf("  -r --remote-socket <name>       Set socket name to <name>.\n" );
  printf("  -t --timeout <seconds>          Set socket read/write timeout in seconds (default 15).\n" );
  printf("  -j --system-charset <name>      Character set used for output (default UTF-8//IGNORE)\n" );
  printf("  -x --print <text|xml>           Choose output format for info commands.\n" );

  printf("Control commands:\n");
  printf("  reload                          Reload configuration.\n");
  printf("  shutdown                        Shutdown DVBlast.\n");

  printf("Status commands:\n");
  printf("  fe_status                       Read frontend status information.\n");

  printf("Demux info commands:\n");
  printf("  get_pat                         Return last PAT table.\n");
  printf("  get_cat                         Return last CAT table.\n");
  printf("  get_nit                         Return last NIT table.\n");
  printf("  get_sdt                         Return last SDT table.\n");
  printf("  get_eit_pf <service_id>         Return last EIT present/following data.\n");
  printf("  get_eit_schedule <service_id>   Return last EIT schedule data.\n");
  printf("  get_pmt <service_id>            Return last PMT table.\n");
  printf("  get_pids                        Return info about all pids.\n");
  printf("  get_pid <pid>                   Return info for chosen pid only.\n");
  printf("\n");
  exit(1);
  }
//}}}

//{{{
int main (int i_argc, char** ppsz_argv) {

  char* client_socket_tmpl = "dvblastctl.clientsock.XXXXXX";
  char* psz_srv_socket = NULL;
  int i;
  char* p_cmd;
  char* p_arg1 = NULL;
  char* p_arg2 = NULL;
  ssize_t i_size;
  struct sockaddr_un sun_client, sun_server;
  uint8_t p_buffer[COMM_BUFFER_SIZE];
  uint8_t* p_data = p_buffer + COMM_HEADER_SIZE;
  uint16_t i_pid = 0;
  struct dvblastctl_option opt = { 0, 0, 0 };

  for ( ; ; ) {
    static const struct option long_options[] = {
      {"remote-socket", required_argument, NULL, 'r'},
      {"timeout", required_argument, NULL, 't'},
      {"system-charset", required_argument, NULL, 'j'},
      {"print", required_argument, NULL, 'x'},
      {"help", no_argument, NULL, 'h'},
      {0, 0, 0, 0}
      };
    int c;
    if ((c = getopt_long(i_argc, ppsz_argv, "r:t:x:j:h", long_options, NULL)) == -1)
      break;

    switch (c) {
      //{{{
      case 'r':
        psz_srv_socket = optarg;
        break;
      //}}}
      //{{{
      case 't':
        i_timeout_seconds = (unsigned int)strtoul(optarg, NULL, 10);
        break;
      //}}}
      //{{{
      case 'j':
        psz_native_charset = optarg;
        break;
      //}}}
      case 'h':
      default:
        usage();
      }
    }

  // Validate commands
  #define usage_error(msg, ...) \
    do { \
      msg_Err( NULL, msg, ##__VA_ARGS__ ); \
      usage(); \
      } while(0)

  p_cmd  = ppsz_argv[optind];
  p_arg1 = ppsz_argv[optind + 1];
  p_arg2 = ppsz_argv[optind + 2];

  if (!psz_srv_socket )
    usage_error ("Remote socket is not set.\n");

  if (!p_cmd)
   usage_error ("Command is not set.\n");

  i = 0;
  do {
    if (streq (ppsz_argv[optind], options[i].opt)) {
      opt = options[i];
        break;
      }
    } while ( options[++i].opt );

  if (!opt.opt)
    usage_error ("Unknown command: %s\n", p_cmd);

  if (opt.nparams == 1 && !p_arg1 )
    usage_error ("%s option needs parameter.\n", opt.opt );

  if (opt.nparams == 2 && (!p_arg1 || !p_arg2) )
    usage_error ("%s option needs two parameters.\n", opt.opt );
  #undef usage_error

  // Create client socket name
  char* tmpdir = getenv ("TMPDIR");
  snprintf (psz_client_socket, PATH_MAX - 1, "%s/%s", tmpdir ? tmpdir : "/tmp", client_socket_tmpl );
  psz_client_socket[PATH_MAX - 1] = '\0';

  int tmp_fd = mkstemp(psz_client_socket);
  if (tmp_fd > -1) {
    close (tmp_fd);
    unlink (psz_client_socket);
    }
  else
    return_error ("Cannot build UNIX socket %s (%s)", psz_client_socket, strerror(errno) );

  if ((i_fd = socket (AF_UNIX, SOCK_DGRAM, 0)) < 0)
    return_error ("Cannot create UNIX socket (%s)", strerror(errno));

  i = COMM_MAX_MSG_CHUNK;
  setsockopt (i_fd, SOL_SOCKET, SO_RCVBUF, &i, sizeof(i));

  memset (&sun_client, 0, sizeof(sun_client));
  sun_client.sun_family = AF_UNIX;
  strncpy (sun_client.sun_path, psz_client_socket, sizeof(sun_client.sun_path) );
  sun_client.sun_path[sizeof(sun_client.sun_path) - 1] = '\0';

  if (bind (i_fd, (struct sockaddr*)&sun_client, SUN_LEN(&sun_client)) < 0)
    return_error ("Cannot bind (%s)", strerror(errno) );

  memset (&sun_server, 0, sizeof(sun_server));
  sun_server.sun_family = AF_UNIX;
  strncpy (sun_server.sun_path, psz_srv_socket, sizeof(sun_server.sun_path));
  sun_server.sun_path[sizeof(sun_server.sun_path) - 1] = '\0';

  p_buffer[0] = COMM_HEADER_MAGIC;
  p_buffer[1] = opt.cmd;
  memset (p_buffer + 2, 0, COMM_HEADER_SIZE - 2);
  i_size = COMM_HEADER_SIZE;

  // Handle commands that send parameters */
  switch (opt.cmd) {
    case CMD_INVALID:
    case CMD_RELOAD:
    case CMD_SHUTDOWN:
    case CMD_FRONTEND_STATUS:
    case CMD_GET_PAT:
    case CMD_GET_CAT:
    case CMD_GET_NIT:
    case CMD_GET_SDT:
    case CMD_GET_PIDS:
      // These commands need no special handling because they have no parameters */
      break;

    case CMD_GET_EIT_PF:
    case CMD_GET_EIT_SCHEDULE:
    //{{{
    case CMD_GET_PMT:
    {
        uint16_t i_sid = atoi(p_arg1);
        i_size = COMM_HEADER_SIZE + 2;
        p_data[0] = (uint8_t)((i_sid >> 8) & 0xff);
        p_data[1] = (uint8_t)(i_sid & 0xff);
        break;
    }
    //}}}
    //{{{
    case CMD_GET_PID:
    {
        i_pid = (uint16_t)atoi(p_arg1);
        i_size = COMM_HEADER_SIZE + 2;
        p_data[0] = (uint8_t)((i_pid >> 8) & 0xff);
        p_data[1] = (uint8_t)(i_pid & 0xff);
        break;
    }
    //}}}
    default:
      // This should not happen */
      return_error ("Unhandled option (%d)", opt.cmd);
    }

  if (i_timeout_seconds > 0) {
    struct timeval tv_timeout = { .tv_sec  = i_timeout_seconds, .tv_usec = 0, };
    if (setsockopt (i_fd, SOL_SOCKET, SO_SNDTIMEO, &tv_timeout, sizeof(tv_timeout)) != 0)
      return_error ("Cannot set SO_SNDTIMEO (%s)", strerror(errno) );

    if (setsockopt (i_fd, SOL_SOCKET, SO_RCVTIMEO, &tv_timeout, sizeof(tv_timeout)) != 0)
      return_error ("Cannot set SO_RCVTIMEO (%s)", strerror(errno));
    }

  // Send command and receive answer
  if (sendto (i_fd, p_buffer, i_size, 0, (struct sockaddr*)&sun_server, SUN_LEN(&sun_server)) < 0)
    return_error ("Cannot send comm socket (%s)", strerror(errno));

  uint32_t i_packet_size = 0;
  uint32_t i_received = 0;
  do {
    i_size = recv (i_fd, p_buffer + i_received, COMM_MAX_MSG_CHUNK, 0);
    if (i_size == -1)
      break;
   if (!i_packet_size) {
      uint32_t* p_packet_size = (uint32_t*)&p_buffer[4];
      i_packet_size = *p_packet_size;
      if (i_packet_size > COMM_BUFFER_SIZE) {
        i_size = -1;
        break;
        }
      }
    i_received += i_size;
    } while (i_received < i_packet_size);

  clean_client_socket();
  if (i_packet_size < COMM_HEADER_SIZE)
    return_error ("Cannot recv from comm socket, size:%"PRIu32" (%s)", i_packet_size, strerror(errno));

  // Process answer */
  if (p_buffer[0] != COMM_HEADER_MAGIC )
    return_error ("Wrong protocol version 0x%x", p_buffer[0]);

  now = mdate();
  ctl_cmd_answer_t c_answer = p_buffer[1];
  switch ( c_answer ) {
    //{{{
    case RET_OK:
        break;
    //}}}
    //{{{
    case RET_ERR:
        return_error( "Request failed" );
        break;
    //}}}
    //{{{
    case RET_HUH:
        return_error( "Internal error" );
        break;
    //}}}
    //{{{
    case RET_NODATA:
        return_error( "No data" );
        break;
    //}}}
    case RET_PAT:
    case RET_CAT:
    case RET_NIT:
    //{{{
    case RET_SDT: {
      uint8_t* p_flat_data = p_buffer + COMM_HEADER_SIZE;
      unsigned int i_flat_data_size = i_packet_size - COMM_HEADER_SIZE;
      uint8_t** pp_sections = psiUnpackSections (p_flat_data, i_flat_data_size);
      if (!pp_sections) {
        return_error( "Error unpacking PSI" );
        break;
        }

      switch( c_answer ) {
        case RET_PAT: pat_table_print( pp_sections, psi_print, NULL, PRINT_TEXT); break;
        case RET_CAT: cat_table_print( pp_sections, psi_print, NULL, PRINT_TEXT ); break;
        case RET_NIT: nit_table_print( pp_sections, psi_print, NULL, psi_iconv, NULL, PRINT_TEXT ); break;
        case RET_SDT: sdt_table_print( pp_sections, psi_print, NULL, psi_iconv, NULL, PRINT_TEXT ); break;
        default: break; /* Can't happen */
        }

      psi_table_free (pp_sections);
      free (pp_sections);
      break;
      }
    //}}}
    case RET_EIT_PF:
    //{{{
    case RET_EIT_SCHEDULE: {
      uint8_t* p_eit_data = p_buffer + COMM_HEADER_SIZE;
      unsigned int i_eit_data_size = i_received - COMM_HEADER_SIZE;
      print_eit (p_eit_data, i_eit_data_size, psi_print, NULL, psi_iconv, NULL, PRINT_TEXT);
      break;
      }
    //}}}
    //{{{
    case RET_PMT: {
      pmt_print (p_data, psi_print, NULL, psi_iconv, NULL, PRINT_TEXT);
      break;
      }
    //}}}
    //{{{
    case RET_PID: {
      print_pids_header();
      print_pid( i_pid, (ts_pid_info_t *)p_data );
      print_pids_footer();
      break;
      }
    //}}}
    //{{{
    case RET_PIDS: {
      print_pids( p_data );
      break;
      }
    //}}}
    //{{{
    case RET_FRONTEND_STATUS: {

      int ret = 1;
      struct ret_frontend_status *p_ret = (struct ret_frontend_status *)&p_buffer[COMM_HEADER_SIZE];
      if ( i_packet_size != COMM_HEADER_SIZE + sizeof(struct ret_frontend_status) )
        return_error( "Bad frontend status" );

      //{{{
      #define PRINT_TYPE( x ) \
          do { \
          printf( "type: %s\n", STRINGIFY(x) ); \
          } while(0)
              switch ( p_ret->info.type )
              {
              case FE_QPSK: PRINT_TYPE(QPSK); break;
              case FE_QAM : PRINT_TYPE(QAM); break;
              case FE_OFDM: PRINT_TYPE(OFDM); break;
              case FE_ATSC: PRINT_TYPE(ATSC); break;
              default     : PRINT_TYPE(UNKNOWN); break;
              }
      #undef PRINT_TYPE
      //}}}
      //{{{
      #define PRINT_INFO( x ) \
          do { \
          printf( "%s: %u\n", STRINGIFY(x), p_ret->info.x ); \
          } while(0)

              printf( "name: %s\n", p_ret->info.name );

              PRINT_INFO( frequency_min );
              PRINT_INFO( frequency_max );
              PRINT_INFO( frequency_stepsize );
              PRINT_INFO( frequency_tolerance );
              PRINT_INFO( symbol_rate_min );
              PRINT_INFO( symbol_rate_max );
              PRINT_INFO( symbol_rate_tolerance );
              PRINT_INFO( notifier_delay );
      #undef PRINT_INFO
      //}}}
      printf("\ncapability list:\n");
      //{{{
      #define PRINT_CAPS( x ) \
        do { \
          if ( p_ret->info.caps & (FE_##x)) \
            printf( "%s\n", STRINGIFY(x) ); \
          } while(0)

              PRINT_CAPS( IS_STUPID );
              PRINT_CAPS( CAN_INVERSION_AUTO );
              PRINT_CAPS( CAN_FEC_1_2 );
              PRINT_CAPS( CAN_FEC_2_3 );
              PRINT_CAPS( CAN_FEC_3_4 );
              PRINT_CAPS( CAN_FEC_4_5 );
              PRINT_CAPS( CAN_FEC_5_6 );
              PRINT_CAPS( CAN_FEC_6_7 );
              PRINT_CAPS( CAN_FEC_7_8 );
              PRINT_CAPS( CAN_FEC_8_9 );
              PRINT_CAPS( CAN_FEC_AUTO );
              PRINT_CAPS( CAN_QPSK );
              PRINT_CAPS( CAN_QAM_16 );
              PRINT_CAPS( CAN_QAM_32 );
              PRINT_CAPS( CAN_QAM_64 );
              PRINT_CAPS( CAN_QAM_128 );
              PRINT_CAPS( CAN_QAM_256 );
              PRINT_CAPS( CAN_QAM_AUTO );
              PRINT_CAPS( CAN_TRANSMISSION_MODE_AUTO );
              PRINT_CAPS( CAN_BANDWIDTH_AUTO );
              PRINT_CAPS( CAN_GUARD_INTERVAL_AUTO );
              PRINT_CAPS( CAN_HIERARCHY_AUTO );
              PRINT_CAPS( CAN_MUTE_TS );

      #if DVBAPI_VERSION >= 301
              PRINT_CAPS( CAN_8VSB );
              PRINT_CAPS( CAN_16VSB );
              PRINT_CAPS( NEEDS_BENDING );
              PRINT_CAPS( CAN_RECOVER );
      #endif
      #if DVBAPI_VERSION >= 500
              PRINT_CAPS( HAS_EXTENDED_CAPS );
      #endif
      #if DVBAPI_VERSION >= 501
              PRINT_CAPS( CAN_2G_MODULATION );
      #endif
      #if DVBAPI_VERSION >= 508
              PRINT_CAPS( CAN_TURBO_FEC );
              PRINT_CAPS( CAN_MULTISTREAM );
      #endif
      #undef PRINT_CAPS
      //}}}
      printf("\nstatus:\n");
      //{{{
      #define PRINT_STATUS( x ) \
          do { \
            if (p_ret->i_status & (FE_##x))  \
              printf ("%s\n", STRINGIFY(x)); \
            } while(0)

              PRINT_STATUS( HAS_SIGNAL );
              PRINT_STATUS( HAS_CARRIER );
              PRINT_STATUS( HAS_VITERBI );
              PRINT_STATUS( HAS_SYNC );
              PRINT_STATUS( HAS_LOCK );
              PRINT_STATUS( REINIT );
      #undef PRINT_STATUS
      //}}}

      if ( p_ret->i_status & FE_HAS_LOCK ) {
        printf ("\nBit error rate: %u\n", p_ret->i_ber);
        printf ("Signal strength: %u\n", p_ret->i_strength);
        printf ("SNR: %u\n", p_ret->i_snr);
        ret = 0;
        }

      exit (ret);
      break;
      }
    //}}}
    default:
      return_error ("Unknown command answer: %u", c_answer );
    }

  if (iconv_handle != (iconv_t)-1) {
    iconv_close(iconv_handle);
    iconv_handle = (iconv_t)-1;
    }

  return 0;
  }
//}}}
