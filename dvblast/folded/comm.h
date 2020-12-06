// comm.h
#pragma once
//{{{  includes
#include <linux/dvb/version.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <bitstream/mpeg/psi.h>
//}}}

#define COMM_HEADER_SIZE 8
#define COMM_BUFFER_SIZE (COMM_HEADER_SIZE + ((PSI_PRIVATE_MAX_SIZE + PSI_HEADER_SIZE) * PSI_TABLE_MAX_SECTIONS))
#define COMM_HEADER_MAGIC 0x49
#define COMM_MAX_MSG_CHUNK 4096
//{{{  enum ctl_cmd_t
typedef enum {
  CMD_INVALID          = 0,
  CMD_RELOAD           = 1,
  CMD_SHUTDOWN         = 2,
  CMD_FRONTEND_STATUS  = 3,
  CMD_GET_PAT          = 10,
  CMD_GET_CAT          = 11,
  CMD_GET_NIT          = 12,
  CMD_GET_SDT          = 13,
  CMD_GET_PMT          = 14, /* arg: service_id (uint16_t) */
  CMD_GET_PIDS         = 15,
  CMD_GET_PID          = 16, /* arg: pid (uint16_t) */
  CMD_GET_EIT_PF       = 19, /* arg: service_id (uint16_t) */
  CMD_GET_EIT_SCHEDULE = 20, /* arg: service_id (uint16_t) */
  } ctl_cmd_t;
//}}}
//{{{  enum ctl_cmd_answer
typedef enum {
  RET_OK              = 0,
  RET_ERR             = 1,
  RET_FRONTEND_STATUS = 2,
  RET_NODATA          = 7,
  RET_PAT             = 8,
  RET_CAT             = 9,
  RET_NIT             = 10,
  RET_SDT             = 11,
  RET_PMT             = 12,
  RET_PIDS            = 13,
  RET_PID             = 14,
  RET_EIT_PF          = 15,
  RET_EIT_SCHEDULE    = 16,
  RET_HUH             = 255,
  } ctl_cmd_answer_t;
//}}}

struct ret_frontend_status {
  struct dvb_frontend_info info;
  fe_status_t i_status;
  uint32_t i_ber;
  uint16_t i_strength, i_snr;
  };

struct cmd_pid_info {
  ts_pid_info_t pids[MAX_PIDS];
  };

void commOpen();
void commClose();
