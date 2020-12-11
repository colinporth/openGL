// dvbRtp.cpp - dvb ts to rtp streams - derived from videoLan dvblast
//{{{  includes
#include "cTsBlockPool.h"
#include "cDvbRtp.h"
#include "cDvb.h"

#include <string.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>

// utils
#include "../../shared/fmt/core.h"
#include "../../shared/utils/cLog.h"

using namespace std;
using namespace fmt;
//}}}
//{{{  defines
constexpr int kRtpHeaderSize = 12;

constexpr int DEFAULT_IPV4_MTU = 1500;
constexpr int DEFAULT_IPV6_MTU = 1280;
constexpr int DEFAULT_PORT = 3001;

constexpr int  MAX_ERRORS = 1000;

constexpr int MAX_POLL_TIMEOUT = 100000;  // 100 ms
constexpr int MIN_POLL_TIMEOUT = 100;     // 100 us

constexpr int MAX_EIT_RETENTION = 500000; // 500 ms

#define WATCHDOG_WAIT 10000000LL
#define WATCHDOG_REFRACTORY_PERIOD 60000000LL

#ifndef IPV6_ADD_MEMBERSHIP
  #define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
  #define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif

//   Bit  1 : Set if DVB conformance tables are inserted
//   Bit  2 : Set if DVB EIT schedule tables are forwarded
constexpr int OUTPUT_DVB   = 0x01;
constexpr int OUTPUT_EPG   = 0x02;
//}}}

namespace {
  //{{{  bitstream utils
  //{{{  ts utils
  #define TS_SIZE             188
  #define TS_HEADER_SIZE      4
  #define TS_HEADER_SIZE_AF   6
  #define TS_HEADER_SIZE_PCR  12

  #define TS_DECLARE(p_ts) uint8_t p_ts[TS_SIZE]

  //{{{
  inline uint8_t *ts_allocate()
  {
      return (uint8_t *)malloc(TS_SIZE * sizeof(uint8_t));
  }
  //}}}
  //{{{
  inline void ts_init(uint8_t *p_ts)
  {
      p_ts[0] = 0x47;
      p_ts[1] = 0x0;
      p_ts[2] = 0x0;
      p_ts[3] = 0x0;
  }
  //}}}

  //{{{
  inline void ts_set_transporterror(uint8_t *p_ts)
  {
      p_ts[1] |= 0x80;
  }
  //}}}
  //{{{
  inline bool ts_get_transporterror(const uint8_t *p_ts)
  {
      return !!(p_ts[1] & 0x80);
  }
  //}}}

  //{{{
  inline void ts_set_unitstart(uint8_t *p_ts)
  {
      p_ts[1] |= 0x40;
  }
  //}}}
  //{{{
  inline bool ts_get_unitstart(const uint8_t *p_ts)
  {
      return !!(p_ts[1] & 0x40);
  }
  //}}}

  //{{{
  inline void ts_set_transportpriority(uint8_t *p_ts)
  {
      p_ts[1] |= 0x20;
  }
  //}}}
  //{{{
  inline bool ts_get_transportpriority(const uint8_t *p_ts)
  {
      return !!(p_ts[1] & 0x20);
  }
  //}}}

  //{{{
  inline void ts_set_pid(uint8_t *p_ts, uint16_t i_pid)
  {
      p_ts[1] &= ~0x1f;
      p_ts[1] |= (i_pid >> 8) & 0x1f;
      p_ts[2] = i_pid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t ts_get_pid(const uint8_t *p_ts)
  {
      return ((p_ts[1] & 0x1f) << 8) | p_ts[2];
  }
  //}}}

  //{{{
  inline void ts_set_cc(uint8_t *p_ts, uint8_t i_cc)
  {
      p_ts[3] &= ~0xf;
      p_ts[3] |= (i_cc & 0xf);
  }
  //}}}
  //{{{
  inline uint8_t ts_get_cc(const uint8_t *p_ts)
  {
      return p_ts[3] & 0xf;
  }
  //}}}

  //{{{
  inline void ts_set_payload(uint8_t *p_ts)
  {
      p_ts[3] |= 0x10;
  }
  //}}}
  //{{{
  inline bool ts_has_payload(const uint8_t *p_ts)
  {
      return !!(p_ts[3] & 0x10);
  }
  //}}}

  //{{{
  inline void ts_set_adaptation(uint8_t *p_ts, uint8_t i_length)
  {
      p_ts[3] |= 0x20;
      p_ts[4] = i_length;
      if (i_length)
          p_ts[5] = 0x0;
      if (i_length > 1)
          memset(&p_ts[6], 0xff, i_length - 1); /* stuffing */
  }
  //}}}
  //{{{
  inline bool ts_has_adaptation(const uint8_t *p_ts)
  {
      return !!(p_ts[3] & 0x20);
  }
  //}}}
  //{{{
  inline uint8_t ts_get_adaptation(const uint8_t *p_ts)
  {
      return p_ts[4];
  }
  //}}}

  //{{{
  inline bool ts_validate(const uint8_t *p_ts)
  {
      return p_ts[0] == 0x47;
  }
  //}}}
  //{{{
  inline void ts_pad(uint8_t *p_ts)
  {
      ts_init(p_ts);
      ts_set_pid(p_ts, 0x1fff);
      ts_set_cc(p_ts, 0);
      ts_set_payload(p_ts);
      memset(p_ts + 4, 0xff, TS_SIZE - 4);
  }
  //}}}

  //{{{
  inline uint8_t *ts_payload(uint8_t *p_ts)
  {
      if (!ts_has_payload(p_ts))
          return p_ts + TS_SIZE;
      if (!ts_has_adaptation(p_ts))
          return p_ts + TS_HEADER_SIZE;
      return p_ts + TS_HEADER_SIZE + 1 + ts_get_adaptation(p_ts);
  }
  //}}}
  //{{{
  inline uint8_t *ts_section(uint8_t *p_ts)
  {
      if (!ts_get_unitstart(p_ts))
          return ts_payload(p_ts);

      return ts_payload(p_ts) + 1; /* pointer_field */
  }
  //}}}
  //{{{
  inline uint8_t *ts_next_section(uint8_t *p_ts)
  {
      uint8_t *p_payload;

      if (!ts_get_unitstart(p_ts))
          return p_ts + TS_SIZE;
      p_payload = ts_payload(p_ts);
      if (p_payload >= p_ts + TS_SIZE)
          return p_ts + TS_SIZE;

      return p_payload + *p_payload + 1; /* pointer_field */
  }
  //}}}

  //{{{
  inline void tsaf_set_discontinuity(uint8_t *p_ts)
  {
      p_ts[5] |= 0x80;
  }
  //}}}
  //{{{
  inline void tsaf_clear_discontinuity(uint8_t *p_ts)
  {
      p_ts[5] &= ~0x80;
  }
  //}}}
  //{{{
  inline bool tsaf_has_discontinuity(const uint8_t *p_ts)
  {
      return !!(p_ts[5] & 0x80);
  }
  //}}}

  //{{{
  inline void tsaf_set_randomaccess(uint8_t *p_ts)
  {
      p_ts[5] |= 0x40;
  }
  //}}}
  //{{{
  inline bool tsaf_has_randomaccess(const uint8_t *p_ts)
  {
      return !!(p_ts[5] & 0x40);
  }
  //}}}

  //{{{
  inline void tsaf_set_streampriority(uint8_t *p_ts)
  {
      p_ts[5] |= 0x20;
  }
  //}}}

  //{{{
  inline void tsaf_set_pcr(uint8_t *p_ts, uint64_t i_pcr)
  {
      p_ts[5] |= 0x10;
      p_ts[6] = (i_pcr >> 25) & 0xff;
      p_ts[7] = (i_pcr >> 17) & 0xff;
      p_ts[8] = (i_pcr >> 9) & 0xff;
      p_ts[9] = (i_pcr >> 1) & 0xff;
      p_ts[10] = 0x7e | ((i_pcr << 7) & 0x80);
      p_ts[11] = 0;
  }
  //}}}
  //{{{
  inline void tsaf_set_pcrext(uint8_t *p_ts, uint16_t i_pcr_ext)
  {
      p_ts[10] |= (i_pcr_ext >> 8) & 0x1;
      p_ts[11] = i_pcr_ext & 0xff;
  }
  //}}}
  //{{{
  inline bool tsaf_has_pcr(const uint8_t *p_ts)
  {
      return !!(p_ts[5] & 0x10);
  }
  //}}}
  //{{{
  inline uint64_t tsaf_get_pcr(const uint8_t *p_ts)
  {
      return ((uint64_t) p_ts[6] << 25) | (p_ts[7] << 17) | (p_ts[8] << 9) | (p_ts[9] << 1) |
             (p_ts[10] >> 7);
  }
  //}}}
  //{{{
  inline uint64_t tsaf_get_pcrext(const uint8_t *p_ts)
  {
      return ((p_ts[10] & 1) << 8) | p_ts[11];
  }
  //}}}

  //{{{

  inline bool ts_check_duplicate(uint8_t i_cc, uint8_t i_last_cc)
  {
      return i_last_cc == i_cc;
  }
  //}}}
  //{{{
  inline bool ts_check_discontinuity(uint8_t i_cc, uint8_t i_last_cc)
  {
      return (i_last_cc + 17 - i_cc) % 16;
  }
  //}}}
  //}}}
  //{{{  psi utils
  #define PSI_HEADER_SIZE         3
  #define PSI_HEADER_SIZE_SYNTAX1 8
  #define PSI_CRC_SIZE            4
  #define PSI_MAX_SIZE            1021
  #define PSI_PRIVATE_MAX_SIZE    4093

  #define PSI_DECLARE(p_table) uint8_t p_table[PSI_MAX_SIZE + PSI_HEADER_SIZE]
  #define PSI_PRIVATE_DECLARE(p_table) uint8_t p_table[PSI_PRIVATE_MAX_SIZE + PSI_HEADER_SIZE]
  //{{{
  /*****************************************************************************
   * p_psi_crc_table
   *****************************************************************************
   * This table is used to compute a PSI CRC byte per byte instead of bit per
   * bit. It's been generated by 'gen_crc' in the 'misc' directory:
   *
   *   uint32_t table[256];
   *   uint32_t i, j, k;
   *
   *   for(i = 0; i < 256; i++)
   *   {
   *     k = 0;
   *     for (j = (i << 24) | 0x800000; j != 0x80000000; j <<= 1)
   *       k = (k << 1) ^ (((k ^ j) & 0x80000000) ? 0x04c11db7 : 0);
   *     table[i] = k;
   *   }
   *
   * A CRC is computed like this:
   *
   *   initialization
   *   --------------
   *   uint32_t i_crc = 0xffffffff;
   *
   *   for each data byte do
   *   ---------------------
   *   i_crc = (i_crc << 8) ^ s_crc32_table[(i_crc >> 24) ^ (data_byte)];
   *****************************************************************************/
  const uint32_t p_psi_crc_table[256] = {
      0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
      0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
      0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
      0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
      0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,
      0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
      0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
      0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
      0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
      0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
      0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,
      0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
      0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,
      0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
      0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
      0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
      0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,
      0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
      0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
      0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
      0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
      0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
      0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,
      0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
      0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,
      0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
      0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
      0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
      0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
      0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
      0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,
      0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
      0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
      0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
      0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
      0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
      0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,
      0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
      0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
      0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
      0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,
      0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
      0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,
      0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
      0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
      0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
      0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,
      0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
      0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
      0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
      0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
      0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
      0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,
      0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
      0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,
      0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
      0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
      0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
      0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
      0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
      0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,
      0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
      0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
      0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
  };
  //}}}

  //{{{
  inline uint8_t *psi_allocate()
  {
      return (uint8_t *)malloc((PSI_MAX_SIZE + PSI_HEADER_SIZE) * sizeof(uint8_t));
  }
  //}}}
  //{{{
  inline uint8_t *psi_private_allocate()
  {
      return (uint8_t *)malloc((PSI_PRIVATE_MAX_SIZE + PSI_HEADER_SIZE) * sizeof(uint8_t));
  }
  //}}}

  //{{{
  inline void psi_set_tableid(uint8_t *p_section, uint8_t i_table_id)
  {
      p_section[0] = i_table_id;
  }
  //}}}
  //{{{
  inline uint8_t psi_get_tableid(const uint8_t *p_section)
  {
      return p_section[0];
  }
  //}}}

  //{{{
  inline void psi_set_syntax(uint8_t *p_section)
  {
      p_section[1] |= 0x80;
  }
  //}}}
  //{{{
  inline bool psi_get_syntax(const uint8_t *p_section)
  {
      return !!(p_section[1] & 0x80);
  }
  //}}}

  //{{{
  inline void psi_init(uint8_t *p_section, bool b_syntax)
  {
      /* set reserved bits */
      p_section[1] = 0x70;
      if (b_syntax) {
          psi_set_syntax(p_section);
          p_section[5] = 0xc0;
      }
  }
  //}}}
  //{{{
  inline void psi_set_length(uint8_t *p_section, uint16_t i_length)
  {
      p_section[1] &= ~0xf;
      p_section[1] |= (i_length >> 8) & 0xf;
      p_section[2] = i_length & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t psi_get_length(const uint8_t *p_section)
  {
      return ((p_section[1] & 0xf) << 8) | p_section[2];
  }
  //}}}

  //{{{
  inline void psi_set_tableidext(uint8_t *p_section, uint16_t i_table_id_ext)
  {
      p_section[3] = i_table_id_ext >> 8;
      p_section[4] = i_table_id_ext & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t psi_get_tableidext(const uint8_t *p_section)
  {
      return (p_section[3] << 8) | p_section[4];
  }
  //}}}

  //{{{
  inline void psi_set_version(uint8_t *p_section, uint8_t i_version)
  {
      p_section[5] = (i_version << 1) | 0xc0;
  }
  //}}}
  //{{{
  inline uint8_t psi_get_version(const uint8_t *p_section)
  {
      return (p_section[5] & 0x3e) >> 1;
  }
  //}}}

  //{{{
  inline void psi_set_current(uint8_t *p_section)
  {
      p_section[5] |= 0x1;
  }
  //}}}
  //{{{
  inline bool psi_get_current(const uint8_t *p_section)
  {
      return !!(p_section[5] & 0x1);
  }
  //}}}

  //{{{
  inline void psi_set_section(uint8_t *p_section, uint8_t i_section)
  {
      p_section[6] = i_section;
  }
  //}}}
  //{{{
  inline uint8_t psi_get_section(const uint8_t *p_section)
  {
      return p_section[6];
  }
  //}}}

  //{{{
  inline void psi_set_lastsection(uint8_t *p_section, uint8_t i_last_section)
  {
      p_section[7] = i_last_section;
  }
  //}}}
  //{{{
  inline uint8_t psi_get_lastsection(const uint8_t *p_section)
  {
      return p_section[7];
  }
  //}}}

  //{{{
  inline void psi_set_crc(uint8_t *p_section)
  {
      uint32_t i_crc = 0xffffffff;
      uint16_t i_end = (((p_section[1] & 0xf) << 8) | p_section[2])
                        + PSI_HEADER_SIZE - PSI_CRC_SIZE;
      uint16_t i;

      for (i = 0; i < i_end; i++)
          i_crc = (i_crc << 8) ^ p_psi_crc_table[(i_crc >> 24) ^ (p_section[i])];

      p_section[i_end] = i_crc >> 24;
      p_section[i_end + 1] = (i_crc >> 16) & 0xff;
      p_section[i_end + 2] = (i_crc >> 8) & 0xff;
      p_section[i_end + 3] = i_crc & 0xff;
  }
  //}}}
  //{{{
  inline bool psi_check_crc(const uint8_t *p_section)
  {
      uint32_t i_crc = 0xffffffff;
      uint16_t i_end = (((p_section[1] & 0xf) << 8) | p_section[2])
                        + PSI_HEADER_SIZE - PSI_CRC_SIZE;
      uint16_t i;

      for (i = 0; i < i_end; i++)
          i_crc = (i_crc << 8) ^ p_psi_crc_table[(i_crc >> 24) ^ (p_section[i])];

      return p_section[i_end] == (i_crc >> 24)
              && p_section[i_end + 1] == ((i_crc >> 16) & 0xff)
              && p_section[i_end + 2] == ((i_crc >> 8) & 0xff)
              && p_section[i_end + 3] == (i_crc & 0xff);
  }
  //}}}
  //{{{
  inline bool psi_validate(const uint8_t *p_section)
  {
      if (psi_get_syntax(p_section)
           && (psi_get_length(p_section) < PSI_HEADER_SIZE_SYNTAX1
                                              - PSI_HEADER_SIZE + PSI_CRC_SIZE))
          return false;

      /* only do the CRC check when it is strictly necessary */

      return true;
  }
  //}}}

  //{{{
  inline bool psi_compare(const uint8_t *p_section1, const uint8_t *p_section2)
  {
      return psi_get_version(p_section1) == psi_get_version(p_section2)
          && psi_get_length(p_section1) == psi_get_length(p_section2)
          && !memcmp(p_section1, p_section2,
                     psi_get_length(p_section1) + PSI_HEADER_SIZE);
  }

  //}}}
  //{{{
  inline void psi_assemble_init(uint8_t **pp_psi_buffer, uint16_t *pi_psi_buffer_used)
  {
      *pp_psi_buffer = NULL;
      *pi_psi_buffer_used = 0;
  }
  //}}}
  //{{{
  inline void psi_assemble_reset(uint8_t **pp_psi_buffer, uint16_t *pi_psi_buffer_used)
  {
      free(*pp_psi_buffer);
      psi_assemble_init(pp_psi_buffer, pi_psi_buffer_used);
  }
  //}}}
  //{{{
  inline bool psi_assemble_empty(uint8_t **pp_psi_buffer, uint16_t *pi_psi_buffer_used) {
      return *pp_psi_buffer == NULL;
  }
  //}}}
  //{{{
  inline uint8_t *psi_assemble_payload(uint8_t **pp_psi_buffer, uint16_t *pi_psi_buffer_used,
                                              const uint8_t **pp_payload, uint8_t *pi_length)
  {
      uint16_t i_remaining_size = PSI_PRIVATE_MAX_SIZE + PSI_HEADER_SIZE
                                   - *pi_psi_buffer_used;
      uint16_t i_copy_size = *pi_length < i_remaining_size ? *pi_length :
                             i_remaining_size;
      uint8_t *p_section = NULL;

      if (*pp_psi_buffer == NULL) {
          if (**pp_payload == 0xff) {
              /* padding table to the end of buffer */
              *pi_length = 0;
              return NULL;
          }
          *pp_psi_buffer = psi_private_allocate();
      }

      memcpy(*pp_psi_buffer + *pi_psi_buffer_used, *pp_payload, i_copy_size);
      *pi_psi_buffer_used += i_copy_size;

      if (*pi_psi_buffer_used >= PSI_HEADER_SIZE) {
          uint16_t i_section_size = psi_get_length(*pp_psi_buffer)
                                     + PSI_HEADER_SIZE;

          if (i_section_size > PSI_PRIVATE_MAX_SIZE) {
              /* invalid section */
              psi_assemble_reset(pp_psi_buffer, pi_psi_buffer_used);
              *pi_length = 0;
              return NULL;
          }
          if (i_section_size <= *pi_psi_buffer_used) {
              p_section = *pp_psi_buffer;
              i_copy_size -= (*pi_psi_buffer_used - i_section_size);
              *pp_psi_buffer = NULL;
              *pi_psi_buffer_used = 0;
          }
      }

      *pp_payload += i_copy_size;
      *pi_length -= i_copy_size;
      return p_section;
  }
  //}}}
  //{{{
  inline void psi_split_end(uint8_t *p_ts, uint8_t *pi_ts_offset)
  {
      if (*pi_ts_offset != TS_SIZE) {
          memset(p_ts + *pi_ts_offset, 0xff, TS_SIZE - *pi_ts_offset);
          *pi_ts_offset = TS_SIZE;
      }
  }
  //}}}
  //{{{
  inline void psi_split_section(uint8_t *p_ts, uint8_t *pi_ts_offset,
                                       const uint8_t *p_section,
                                       uint16_t *pi_section_offset)
  {
      uint16_t i_section_length = psi_get_length(p_section) + PSI_HEADER_SIZE
                                   - *pi_section_offset;
      uint8_t i_ts_length, i_copy;

      if (!*pi_ts_offset) {
          ts_init(p_ts);
          ts_set_payload(p_ts);
          *pi_ts_offset = ts_payload(p_ts) - p_ts;
      }

      if (!*pi_section_offset) {
          if (TS_SIZE - *pi_ts_offset < 2) {
              psi_split_end(p_ts, pi_ts_offset);
              return;
          }
          if (!ts_get_unitstart(p_ts)) {
              uint8_t *p_payload = ts_payload(p_ts);
              uint8_t i_payload_length = *pi_ts_offset - (p_payload - p_ts);
              if (i_payload_length)
                  memmove(p_payload + 1, p_payload, i_payload_length);
              (*pi_ts_offset)++;
              *p_payload = i_payload_length; /* pointer_field */
              ts_set_unitstart(p_ts);
          }
      }
      i_ts_length = TS_SIZE - *pi_ts_offset;

      i_copy = i_ts_length < i_section_length ?
               i_ts_length : i_section_length;
      memcpy(p_ts + *pi_ts_offset, p_section + *pi_section_offset, i_copy);
      *pi_ts_offset += i_copy;
      *pi_section_offset += i_copy;
  }
  //}}}

  #define PSI_TABLE_MAX_SECTIONS         256
  #define PSI_TABLE_DECLARE(pp_table) uint8_t *pp_table[PSI_TABLE_MAX_SECTIONS]

  //{{{
  inline uint8_t **psi_table_allocate()
  {
      return (uint8_t **)malloc(PSI_TABLE_MAX_SECTIONS * sizeof(uint8_t *));
  }
  //}}}
  //{{{
  inline void psi_table_init(uint8_t **pp_sections)
  {
      int i;
      for (i = 0; i < PSI_TABLE_MAX_SECTIONS; i++)
          pp_sections[i] = NULL;
  }
  //}}}
  //{{{
  inline void psi_table_free(uint8_t **pp_sections)
  {
      int i;
      for (i = 0; i < PSI_TABLE_MAX_SECTIONS; i++)
          free(pp_sections[i]);
  }
  //}}}
  //{{{

  inline bool psi_table_validate(uint8_t * const *pp_sections)
  {
      return pp_sections[0] != NULL;
  }
  //}}}
  //{{{
  inline void psi_table_copy(uint8_t **pp_dest, uint8_t **pp_src)
  {
      memcpy(pp_dest, pp_src, PSI_TABLE_MAX_SECTIONS * sizeof(uint8_t *));
  }
  //}}}

  #define psi_table_get_tableid(pp_sections) psi_get_tableid(pp_sections[0])
  #define psi_table_get_version(pp_sections) psi_get_version(pp_sections[0])
  #define psi_table_get_current(pp_sections) psi_get_current(pp_sections[0])
  #define psi_table_get_lastsection(pp_sections) psi_get_lastsection(pp_sections[0])
  #define psi_table_get_tableidext(pp_sections) psi_get_tableidext(pp_sections[0])

  //{{{
  inline bool psi_table_section(uint8_t **pp_sections, uint8_t *p_section)
  {
      uint8_t i_section = psi_get_section( p_section );
      uint8_t i_last_section = psi_get_lastsection( p_section );
      uint8_t i_version = psi_get_version( p_section );
      uint16_t i_tableidext = psi_get_tableidext( p_section );
      int i;

      free(pp_sections[i_section]);
      pp_sections[i_section] = p_section;

      for (i = 0; i <= i_last_section; i++) {
          uint8_t *p = pp_sections[i];
          if (p == NULL)
              return false;
          if (psi_get_lastsection(p) != i_last_section
               || psi_get_version(p) != i_version
               || psi_get_tableidext(p) != i_tableidext)
              return false;
      }

      /* free spurious, invalid sections */
      for (; i < PSI_TABLE_MAX_SECTIONS; i++) {
          free(pp_sections[i]);
          pp_sections[i] = NULL;
      }

      /* a new, full table is available */
      return true;
  }
  //}}}
  //{{{
  inline uint8_t *psi_table_get_section(uint8_t **pp_sections, uint8_t n)
  {
      return pp_sections[n];
  }
  //}}}
  //{{{
  inline bool psi_table_compare(uint8_t **pp_sections1,
                                       uint8_t **pp_sections2)
  {
      uint8_t i_last_section = psi_table_get_lastsection(pp_sections1);
      uint8_t i;

      if (i_last_section != psi_table_get_lastsection(pp_sections2))
          return false;

      for (i = 0; i <= i_last_section; i++) {
          const uint8_t *p_section1 = psi_table_get_section(pp_sections1, i);
          const uint8_t *p_section2 = psi_table_get_section(pp_sections2, i);
          if (!psi_compare(p_section1, p_section2))
              return false;
      }

      return true;
  }
  //}}}
  //}}}
  //{{{  descriptors
  #define DESC_HEADER_SIZE        2
  //{{{
  inline void desc_set_tag(uint8_t *p_desc, uint8_t i_tag)
  {
      p_desc[0] = i_tag;
  }
  //}}}
  //{{{
  inline uint8_t desc_get_tag(const uint8_t *p_desc)
  {
      return p_desc[0];
  }
  //}}}
  //{{{
  inline void desc_set_length(uint8_t *p_desc, uint8_t i_length)
  {
      p_desc[1] = i_length;
  }
  //}}}
  //{{{
  inline uint8_t desc_get_length(const uint8_t *p_desc)
  {
      return p_desc[1];
  }

  //}}}

  //{{{
  inline uint8_t *descl_get_desc(uint8_t *p_descl, uint16_t i_length,
                                        uint16_t n)
  {
      uint8_t *p_desc = p_descl;

      while (n) {
          if (p_desc + DESC_HEADER_SIZE - p_descl > i_length) return NULL;
          p_desc += DESC_HEADER_SIZE + desc_get_length(p_desc);
          n--;
      }
      if (p_desc - p_descl >= i_length) return NULL;
      return p_desc;
  }
  //}}}
  //{{{
  inline bool descl_validate(const uint8_t *p_descl, uint16_t i_length)
  {
      const uint8_t *p_desc = p_descl;

      while (p_desc + DESC_HEADER_SIZE - p_descl <= i_length)
          p_desc += DESC_HEADER_SIZE + desc_get_length(p_desc);

      return (p_desc - p_descl == i_length);
  }
  //}}}

  #define DESCS_HEADER_SIZE       2
  #define DESCS_MAX_SIZE          4095
  //{{{
  inline void descs_set_length(uint8_t *p_descs, uint16_t i_length)
  {
      p_descs[0] &= 0xf0;
      p_descs[0] |= (i_length >> 8) & 0xf;
      p_descs[1] = i_length & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t descs_get_length(const uint8_t *p_descs)
  {
      return ((p_descs[0] & 0xf) << 8) | p_descs[1];
  }
  //}}}

  //{{{
  inline uint8_t *descs_get_desc(uint8_t *p_descs, uint16_t n)
  {
      return descl_get_desc(p_descs + DESCS_HEADER_SIZE,
                            descs_get_length(p_descs), n);
  }
  //}}}
  //{{{
  inline bool descs_validate_desc(const uint8_t *p_descs, const uint8_t *p_desc, uint8_t i_desclength)
  {
      uint16_t i_descs_length = descs_get_length(p_descs);
      return (p_desc + i_desclength <= p_descs + i_descs_length);
  }
  //}}}

  //{{{
  inline bool descs_validate(const uint8_t *p_descs)
  {
      return descl_validate(p_descs + DESCS_HEADER_SIZE,
                            descs_get_length(p_descs));
  }
  //}}}
  //}}}

  //{{{  pat
  #define PAT_PID                 0x0
  #define PAT_TABLE_ID            0x0
  #define PAT_HEADER_SIZE         PSI_HEADER_SIZE_SYNTAX1
  #define PAT_PROGRAM_SIZE        4

  #define pat_set_tsid psi_set_tableidext
  #define pat_get_tsid psi_get_tableidext

  //{{{
  inline void pat_init(uint8_t *p_pat)
  {
      psi_init(p_pat, true);
      psi_set_tableid(p_pat, PAT_TABLE_ID);
      p_pat[1] &= ~0x40;
  }
  //}}}
  //{{{
  inline void pat_set_length(uint8_t *p_pat, uint16_t i_pat_length)
  {
      psi_set_length(p_pat, PAT_HEADER_SIZE + PSI_CRC_SIZE - PSI_HEADER_SIZE
                      + i_pat_length);
  }
  //}}}
  //{{{
  inline void patn_init(uint8_t *p_pat_n)
  {
      p_pat_n[2] = 0xe0;
  }
  //}}}

  //{{{
  inline void patn_set_program(uint8_t *p_pat_n, uint16_t i_program)
  {
      p_pat_n[0] = i_program >> 8;
      p_pat_n[1] = i_program & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t patn_get_program(const uint8_t *p_pat_n)
  {
      return (p_pat_n[0] << 8) | p_pat_n[1];
  }
  //}}}

  //{{{
  inline void patn_set_pid(uint8_t *p_pat_n, uint16_t i_pid)
  {
      p_pat_n[2] &= ~0x1f;
      p_pat_n[2] |= i_pid >> 8;
      p_pat_n[3] = i_pid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t patn_get_pid(const uint8_t *p_pat_n)
  {
      return ((p_pat_n[2] & 0x1f) << 8) | p_pat_n[3];
  }
  //}}}

  //{{{
  inline uint8_t *pat_get_program(uint8_t *p_pat, uint8_t n)
  {
      uint8_t *p_pat_n = p_pat + PAT_HEADER_SIZE + n * PAT_PROGRAM_SIZE;
      if (p_pat_n + PAT_PROGRAM_SIZE - p_pat
           > psi_get_length(p_pat) + PSI_HEADER_SIZE - PSI_CRC_SIZE)
          return NULL;
      return p_pat_n;
  }
  //}}}

  //{{{
  inline bool pat_validate(const uint8_t *p_pat)
  {
      if (!psi_get_syntax(p_pat) || psi_get_tableid(p_pat) != PAT_TABLE_ID)
          return false;
      if ((psi_get_length(p_pat) - PAT_HEADER_SIZE + PSI_HEADER_SIZE
           - PSI_CRC_SIZE) % PAT_PROGRAM_SIZE)
          return false;

      return true;
  }
  //}}}

  //{{{
  inline uint8_t *pat_table_find_program(uint8_t **pp_sections, uint16_t i_program)
  {
      uint8_t i_last_section = psi_table_get_lastsection(pp_sections);
      uint8_t i;

      for (i = 0; i <= i_last_section; i++) {
          uint8_t *p_section = psi_table_get_section(pp_sections, i);
          uint8_t *p_program;
          int j = 0;

          while ((p_program = pat_get_program(p_section, j)) != NULL) {
              j++;
              if (patn_get_program(p_program) == i_program)
                  return p_program;
          }
      }

      return NULL;
  }
  //}}}
  //{{{
  inline bool pat_table_validate(uint8_t **pp_sections)
  {
      uint8_t i_last_section = psi_table_get_lastsection(pp_sections);
      uint8_t i;

      for (i = 0; i <= i_last_section; i++) {
          uint8_t *p_section = psi_table_get_section(pp_sections, i);
          uint8_t *p_program;
          int j = 0;

          if (!psi_check_crc(p_section))
              return false;

          while ((p_program = pat_get_program(p_section, j)) != NULL) {
              uint8_t *p_program2 = pat_table_find_program(pp_sections,
                                        patn_get_program(p_program));
              j++;
              /* check that the program number is not already in the table
               * with another PID */
              if (patn_get_pid(p_program) != patn_get_pid(p_program2))
                  return false;
          }
      }

      return true;
  }
  //}}}
  //}}}

  //{{{  pmt
  // Program Map Table
  #define PMT_TABLE_ID            0x2
  #define PMT_HEADER_SIZE         (PSI_HEADER_SIZE_SYNTAX1 + 4)
  #define PMT_ES_SIZE             5
  #define pmt_set_program psi_set_tableidext
  #define pmt_get_program psi_get_tableidext
  //{{{  stream types
  #define PMT_STREAMTYPE_VIDEO_MPEG1      0x1
  #define PMT_STREAMTYPE_VIDEO_MPEG2      0x2
  #define PMT_STREAMTYPE_AUDIO_MPEG1      0x3
  #define PMT_STREAMTYPE_AUDIO_MPEG2      0x4
  #define PMT_STREAMTYPE_PRIVATE_PSI      0x5
  #define PMT_STREAMTYPE_PRIVATE_PES      0x6
  #define PMT_STREAMTYPE_MHEG             0x7
  #define PMT_STREAMTYPE_DSM_CC           0x8
  #define PMT_STREAMTYPE_H222_1           0x9
  #define PMT_STREAMTYPE_13818_6_A        0xa
  #define PMT_STREAMTYPE_13818_6_B        0xb
  #define PMT_STREAMTYPE_13818_6_C        0xc
  #define PMT_STREAMTYPE_13818_6_D        0xd
  #define PMT_STREAMTYPE_H222_0_AUX       0xe
  #define PMT_STREAMTYPE_AUDIO_ADTS       0xf
  #define PMT_STREAMTYPE_VIDEO_MPEG4      0x10
  #define PMT_STREAMTYPE_AUDIO_LATM       0x11
  #define PMT_STREAMTYPE_SL_PES           0x12
  #define PMT_STREAMTYPE_SL_14496         0x13
  #define PMT_STREAMTYPE_SDP              0x14
  #define PMT_STREAMTYPE_META_PES         0x15
  #define PMT_STREAMTYPE_META_PSI         0x16
  #define PMT_STREAMTYPE_META_DC          0x17
  #define PMT_STREAMTYPE_META_OC          0x18
  #define PMT_STREAMTYPE_META_SDP         0x19
  #define PMT_STREAMTYPE_IPMP_13818_11    0x1a
  #define PMT_STREAMTYPE_VIDEO_AVC        0x1b
  #define PMT_STREAMTYPE_VIDEO_HEVC       0x24
  #define PMT_STREAMTYPE_VIDEO_AVS        0x42
  #define PMT_STREAMTYPE_IPMP             0x7f
  #define PMT_STREAMTYPE_ATSC_A52         0x81
  #define PMT_STREAMTYPE_SCTE_35          0x86
  #define PMT_STREAMTYPE_ATSC_A52E        0x87
  //}}}

  //{{{
  inline void pmt_init(uint8_t *p_pmt)
  {
      psi_init(p_pmt, true);
      psi_set_tableid(p_pmt, PMT_TABLE_ID);
      p_pmt[1] &= ~0x40;
      psi_set_section(p_pmt, 0);
      psi_set_lastsection(p_pmt, 0);
      p_pmt[8] = 0xe0;
      p_pmt[10] = 0xf0;
  }
  //}}}
  //{{{
  inline void pmt_set_length(uint8_t *p_pmt, uint16_t i_pmt_length)
  {
      psi_set_length(p_pmt, PMT_HEADER_SIZE + PSI_CRC_SIZE - PSI_HEADER_SIZE
                      + i_pmt_length);
  }
  //}}}

  //{{{
  inline void pmt_set_pcrpid(uint8_t *p_pmt, uint16_t i_pcr_pid)
  {
      p_pmt[8] &= ~0x1f;
      p_pmt[8] |= i_pcr_pid >> 8;
      p_pmt[9] = i_pcr_pid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t pmt_get_pcrpid(const uint8_t *p_pmt)
  {
      return ((p_pmt[8] & 0x1f) << 8) | p_pmt[9];
  }
  //}}}

  //{{{
  inline void pmt_set_desclength(uint8_t *p_pmt, uint16_t i_length)
  {
      p_pmt[10] &= ~0xf;
      p_pmt[10] |= i_length >> 8;
      p_pmt[11] = i_length & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t pmt_get_desclength(const uint8_t *p_pmt)
  {
      return ((p_pmt[10] & 0xf) << 8) | p_pmt[11];
  }
  //}}}

  //{{{
  inline uint8_t *pmt_get_descs(uint8_t *p_pmt)
  {
      return &p_pmt[10];
  }
  //}}}

  //{{{
  inline void pmtn_init(uint8_t *p_pmt_n)
  {
      p_pmt_n[1] = 0xe0;
      p_pmt_n[3] = 0xf0;
  }
  //}}}

  //{{{
  inline void pmtn_set_streamtype(uint8_t *p_pmt_n, uint8_t i_stream_type)
  {
      p_pmt_n[0] = i_stream_type;
  }
  //}}}
  //{{{
  inline uint8_t pmtn_get_streamtype(const uint8_t *p_pmt_n)
  {
      return p_pmt_n[0];
  }
  //}}}
  //{{{
  inline const char *pmt_get_streamtype_txt(uint8_t i_stream_type) {
      /* ISO/IEC 13818-1 | Table 2-36 - Stream type assignments */
      if (i_stream_type == 0)
          return "Reserved";
      switch (i_stream_type) {
          case 0x01: return "11172-2 video (MPEG-1)";
          case 0x02: return "13818-2 video (MPEG-2)";
          case 0x03: return "11172-3 audio (MPEG-1)";
          case 0x04: return "13818-3 audio (MPEG-2)";
          case 0x05: return "13818-1 private sections";
          case 0x06: return "13818-1 PES private data";
          case 0x07: return "13522 MHEG";
          case 0x08: return "H.222.0/13818-1 Annex A - DSM CC";
          case 0x09: return "H.222.1";
          case 0x0A: return "13818-6 type A";
          case 0x0B: return "13818-6 type B";
          case 0x0C: return "13818-6 type C";
          case 0x0D: return "13818-6 type D";
          case 0x0E: return "H.222.0/13818-1 auxiliary";
          case 0x0F: return "13818-7 Audio with ADTS transport syntax";
          case 0x10: return "14496-2 Visual (MPEG-4 part 2 video)";
          case 0x11: return "14496-3 Audio with LATM transport syntax (14496-3/AMD 1)";
          case 0x12: return "14496-1 SL-packetized or FlexMux stream in PES packets";
          case 0x13: return "14496-1 SL-packetized or FlexMux stream in 14496 sections";
          case 0x14: return "ISO/IEC 13818-6 Synchronized Download Protocol";
          case 0x15: return "Metadata in PES packets";
          case 0x16: return "Metadata in metadata_sections";
          case 0x17: return "Metadata in 13818-6 Data Carousel";
          case 0x18: return "Metadata in 13818-6 Object Carousel";
          case 0x19: return "Metadata in 13818-6 Synchronized Download Protocol";
          case 0x1A: return "13818-11 MPEG-2 IPMP stream";
          case 0x1B: return "H.264/14496-10 video (MPEG-4/AVC)";
          case 0x24: return "H.265 video (MPEG-H/HEVC)";
          case 0x42: return "AVS Video";
          case 0x7F: return "IPMP stream";
          case 0x81: return "ATSC A/52";
          case 0x86: return "SCTE 35 Splice Information Table";
          case 0x87: return "ATSC A/52e";
          default  : return "Unknown";
      }
  }
  //}}}

  //{{{
  inline void pmtn_set_pid(uint8_t *p_pmt_n, uint16_t i_pid)
  {
      p_pmt_n[1] &= ~0x1f;
      p_pmt_n[1] |= i_pid >> 8;
      p_pmt_n[2] = i_pid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t pmtn_get_pid(const uint8_t *p_pmt_n)
  {
      return ((p_pmt_n[1] & 0x1f) << 8) | p_pmt_n[2];
  }
  //}}}

  //{{{
  inline void pmtn_set_desclength(uint8_t *p_pmt_n, uint16_t i_length)
  {
      p_pmt_n[3] &= ~0xf;
      p_pmt_n[3] |= i_length >> 8;
      p_pmt_n[4] = i_length & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t pmtn_get_desclength(const uint8_t *p_pmt_n)
  {
      return ((p_pmt_n[3] & 0xf) << 8) | p_pmt_n[4];
  }
  //}}}
  //{{{
  inline uint8_t *pmtn_get_descs(uint8_t *p_pmt_n)
  {
      return &p_pmt_n[3];
  }
  //}}}
  //{{{
  inline uint8_t *pmt_get_es(uint8_t *p_pmt, uint8_t n)
  {
      uint16_t i_section_size = psi_get_length(p_pmt) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      uint8_t *p_pmt_n = p_pmt + PMT_HEADER_SIZE + pmt_get_desclength(p_pmt);
      if (p_pmt_n - p_pmt > i_section_size) return NULL;

      while (n) {
          if (p_pmt_n + PMT_ES_SIZE - p_pmt > i_section_size) return NULL;
          p_pmt_n += PMT_ES_SIZE + pmtn_get_desclength(p_pmt_n);
          n--;
      }
      if (p_pmt_n - p_pmt >= i_section_size) return NULL;
      return p_pmt_n;
  }
  //}}}

  //{{{
  inline bool pmt_validate_es(const uint8_t *p_pmt, const uint8_t *p_pmt_n, uint16_t i_desclength)
  {
      uint16_t i_section_size = psi_get_length(p_pmt) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      return (p_pmt_n + PMT_ES_SIZE + i_desclength
               <= p_pmt + i_section_size);
  }
  //}}}
  //{{{
  inline bool pmt_validate(const uint8_t *p_pmt)
  {
      uint16_t i_section_size = psi_get_length(p_pmt) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      const uint8_t *p_pmt_n;

      if (!psi_get_syntax(p_pmt) || psi_get_section(p_pmt)
           || psi_get_lastsection(p_pmt)
           || psi_get_tableid(p_pmt) != PMT_TABLE_ID)
          return false;

      if (!psi_check_crc(p_pmt))
          return false;

      if (i_section_size < PMT_HEADER_SIZE
           || i_section_size < PMT_HEADER_SIZE + pmt_get_desclength(p_pmt))
          return false;

      if (!descs_validate(p_pmt + 10))
          return false;

      p_pmt_n = p_pmt + PMT_HEADER_SIZE + pmt_get_desclength(p_pmt);

      while (p_pmt_n + PMT_ES_SIZE - p_pmt <= i_section_size
              && p_pmt_n + PMT_ES_SIZE + pmtn_get_desclength(p_pmt_n) - p_pmt
                  <= i_section_size) {
          if (!descs_validate(p_pmt_n + 3))
              return false;

          p_pmt_n += PMT_ES_SIZE + pmtn_get_desclength(p_pmt_n);
      }

      return (p_pmt_n - p_pmt == i_section_size);
  }
  //}}}

  //{{{
  inline uint8_t *pmt_find_es(uint8_t *p_pmt, uint16_t i_pid)
  {
      uint8_t *p_es;
      uint8_t j = 0;

      while ((p_es = pmt_get_es(p_pmt, j)) != NULL) {
          j++;
          if (pmtn_get_pid(p_es) == i_pid)
              return p_es;
      }

      return NULL;
  }
  //}}}
  //}}}
  //{{{  tdt
  // Time and Date Table
  #define TDT_PID                 0x14
  #define TDT_TABLE_ID            0x70
  #define TDT_HEADER_SIZE         (PSI_HEADER_SIZE + 5)

  //{{{
  inline void tdt_init(uint8_t *p_tdt)
  {
      psi_init(p_tdt, false);
      psi_set_tableid(p_tdt, TDT_TABLE_ID);
      psi_set_length(p_tdt, TDT_HEADER_SIZE - PSI_HEADER_SIZE);
  }
  //}}}
  //{{{
  inline void tdt_set_utc(uint8_t *p_tdt, uint64_t i_utc)
  {
      p_tdt[3] = (i_utc >> 32) & 0xff;
      p_tdt[4] = (i_utc >> 24) & 0xff;
      p_tdt[5] = (i_utc >> 16) & 0xff;
      p_tdt[6] = (i_utc >> 8) & 0xff;
      p_tdt[7] = i_utc & 0xff;
  }
  //}}}
  //{{{
  inline uint64_t tdt_get_utc(const uint8_t *p_tdt)
  {
      return (uint64_t)(((uint64_t)p_tdt[3] << 32) | ((uint64_t)p_tdt[4] << 24) |
                        ((uint64_t)p_tdt[5] << 16) | ((uint64_t)p_tdt[6] << 8) | p_tdt[7]);
  }
  //}}}
  //{{{
  inline bool tdt_validate(const uint8_t *p_tdt)
  {
      uint16_t i_section_size = psi_get_length(p_tdt) + PSI_HEADER_SIZE;
      uint8_t i_tid = psi_get_tableid(p_tdt);

      if (psi_get_syntax(p_tdt) || i_tid != TDT_TABLE_ID
           || i_section_size < TDT_HEADER_SIZE)
          return false;

      return true;
  }
  //}}}
  //}}}
  //{{{  eit
  #define EIT_PID                         0x12
  #define EIT_TABLE_ID_PF_ACTUAL          0x4e
  #define EIT_TABLE_ID_PF_OTHER           0x4f
  #define EIT_TABLE_ID_SCHED_ACTUAL_FIRST 0x50
  #define EIT_TABLE_ID_SCHED_ACTUAL_LAST  0x5f
  #define EIT_TABLE_ID_SCHED_OTHER_FIRST  0x60
  #define EIT_TABLE_ID_SCHED_OTHER_LAST   0x6f
  #define EIT_HEADER_SIZE                 (PSI_HEADER_SIZE_SYNTAX1 + 6)
  #define EIT_EVENT_SIZE                  12

  #define eit_set_sid psi_set_tableidext
  #define eit_get_sid psi_get_tableidext

  //{{{
  inline void eit_init(uint8_t *p_eit, bool b_actual)
  {
      psi_init(p_eit, true);
      psi_set_tableid(p_eit, b_actual ? EIT_TABLE_ID_PF_ACTUAL :
                      EIT_TABLE_ID_PF_OTHER);
  }
  //}}}
  //{{{
  inline void eit_set_length(uint8_t *p_eit, uint16_t i_eit_length)
  {
      psi_set_length(p_eit, EIT_HEADER_SIZE + PSI_CRC_SIZE - PSI_HEADER_SIZE
                      + i_eit_length);
  }
  //}}}

  //{{{
  inline void eit_set_tsid(uint8_t *p_eit, uint16_t i_tsid)
  {
      p_eit[8] = i_tsid >> 8;
      p_eit[9] = i_tsid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t eit_get_tsid(const uint8_t *p_eit)
  {
      return (p_eit[8] << 8) | p_eit[9];
  }
  //}}}

  //{{{
  inline void eit_set_onid(uint8_t *p_eit, uint16_t i_onid)
  {
      p_eit[10] = i_onid >> 8;
      p_eit[11] = i_onid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t eit_get_onid(const uint8_t *p_eit)
  {
      return (p_eit[10] << 8) | p_eit[11];
  }
  //}}}

  //{{{
  inline void eit_set_segment_last_sec_number(uint8_t *p_eit, uint8_t i_segment_last_sec_number)
  {
      p_eit[12] = i_segment_last_sec_number;
  }
  //}}}
  //{{{
  inline uint8_t eit_get_segment_last_sec_number(const uint8_t *p_eit)
  {
      return p_eit[12];
  }
  //}}}

  //{{{
  inline void eit_set_last_table_id(uint8_t *p_eit, uint8_t i_last_table_id)
  {
      p_eit[13] = i_last_table_id;
  }
  //}}}
  //{{{
  inline uint8_t eit_get_last_table_id(const uint8_t *p_eit)
  {
      return p_eit[13];
  }
  //}}}

  //{{{
  inline void eitn_init(uint8_t *p_eit_n)
  {
      p_eit_n[10] = 0;
  }
  //}}}
  //{{{
  inline uint16_t eitn_get_event_id(const uint8_t *p_eit_n)
  {
      return (p_eit_n[0] << 8) | p_eit_n[1];
  }
  //}}}
  //{{{
  inline void eitn_set_event_id(uint8_t *p_eit_n, uint16_t i_event_id)
  {
      p_eit_n[0] = i_event_id >> 8;
      p_eit_n[1] = i_event_id & 0xff;
  }
  //}}}

  //{{{
  inline uint64_t eitn_get_start_time(const uint8_t *p_eit_n)
  {
      return (uint64_t)(((uint64_t)p_eit_n[2] << 32) | ((uint64_t)p_eit_n[3] << 24) |
                        ((uint64_t)p_eit_n[4] << 16) | ((uint64_t)p_eit_n[5] << 8) | p_eit_n[6]);
  }
  //}}}
  //{{{
  inline void eitn_set_start_time(uint8_t *p_eit_n, uint64_t i_start_time)
  {
      p_eit_n[2] = (i_start_time >> 32) & 0xff;
      p_eit_n[3] = (i_start_time >> 24) & 0xff;
      p_eit_n[4] = (i_start_time >> 16) & 0xff;
      p_eit_n[5] = (i_start_time >>  8) & 0xff;
      p_eit_n[6] = i_start_time         & 0xff;
  }
  //}}}

  //{{{
  inline uint32_t eitn_get_duration_bcd(const uint8_t *p_eit_n)
  {
      return ((p_eit_n[7] << 16) | (p_eit_n[8] << 8)) | p_eit_n[9];
  }
  //}}}
  //{{{
  inline void eitn_set_duration_bcd(uint8_t *p_eit_n, uint32_t i_duration_bcd)
  {
      p_eit_n[7] = (i_duration_bcd >> 16) & 0xff;
      p_eit_n[8] = (i_duration_bcd >>  8) & 0xff;
      p_eit_n[9] = i_duration_bcd         & 0xff;
  }
  //}}}

  //{{{
  inline uint8_t eitn_get_running(const uint8_t *p_eit_n)
  {
      return p_eit_n[10] >> 5;
  }
  //}}}
  //{{{
  inline void eitn_set_running(uint8_t *p_eit_n, uint8_t i_running_status)
  {
      p_eit_n[10] = (p_eit_n[10] & 0x1f) | (i_running_status << 5);
  }
  //}}}

  //{{{
  inline bool eitn_get_ca(const uint8_t *p_eit_n)
  {
      return (p_eit_n[10] & 0x10) == 0x10;
  }
  //}}}
  //{{{
  inline void eitn_set_ca(uint8_t *p_eit_n)
  {
      p_eit_n[10] |= 0x10;
  }
  //}}}

  //{{{
  inline uint16_t eitn_get_desclength(const uint8_t *p_eit_n)
  {
      return ((p_eit_n[10] & 0xf) << 8) | p_eit_n[11];
  }
  //}}}
  //{{{
  inline void eitn_set_desclength(uint8_t *p_eit_n, uint16_t i_length)
  {
      p_eit_n[10] &= ~0xf;
      p_eit_n[10] |= (i_length >> 8) & 0xf;
      p_eit_n[11] = i_length & 0xff;
  }
  //}}}

  //{{{
  inline uint8_t *eitn_get_descs(uint8_t *p_eit_n)
  {
      return &p_eit_n[10];
  }
  //}}}
  //{{{
  inline uint8_t *eit_get_event(uint8_t *p_eit, uint8_t n)
  {
      uint16_t i_section_size = psi_get_length(p_eit) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      uint8_t *p_eit_n = p_eit + EIT_HEADER_SIZE;
      if (p_eit_n - p_eit > i_section_size) return NULL;

      while (n) {
          if (p_eit_n + EIT_EVENT_SIZE - p_eit > i_section_size) return NULL;
          p_eit_n += EIT_EVENT_SIZE + eitn_get_desclength(p_eit_n);
          n--;
      }
      if (p_eit_n - p_eit >= i_section_size) return NULL;

      return p_eit_n;
  }
  //}}}
  //{{{
  inline bool eit_validate_event(const uint8_t *p_eit,
                                        const uint8_t *p_eit_n,
                                        uint16_t i_desclength)
  {
      uint16_t i_section_size = psi_get_length(p_eit) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      return (p_eit_n + EIT_EVENT_SIZE + i_desclength
               <= p_eit + i_section_size);
  }
  //}}}

  //{{{
  inline bool eit_validate(const uint8_t *p_eit)
  {
      uint16_t i_section_size = psi_get_length(p_eit) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      uint8_t i_tid = psi_get_tableid(p_eit);
      const uint8_t *p_eit_n;

      if (!psi_get_syntax(p_eit)
           || (i_tid != EIT_TABLE_ID_PF_ACTUAL
                && i_tid != EIT_TABLE_ID_PF_OTHER
                && !(i_tid >= EIT_TABLE_ID_SCHED_ACTUAL_FIRST
                      && i_tid <= EIT_TABLE_ID_SCHED_ACTUAL_LAST)
                && !(i_tid >= EIT_TABLE_ID_SCHED_OTHER_FIRST
                      && i_tid <= EIT_TABLE_ID_SCHED_OTHER_LAST)))
          return false;

      if (!psi_check_crc(p_eit))
          return false;

      p_eit_n = p_eit + EIT_HEADER_SIZE;

      while (p_eit_n + EIT_EVENT_SIZE - p_eit <= i_section_size
              && p_eit_n + EIT_EVENT_SIZE + eitn_get_desclength(p_eit_n) - p_eit
                  <= i_section_size) {
          if (!descs_validate(p_eit_n + 10))
              return false;

          p_eit_n += EIT_EVENT_SIZE + eitn_get_desclength(p_eit_n);
      }

      return (p_eit_n - p_eit == i_section_size);
  }
  //}}}
  //}}}
  //{{{  rst
  // Running Status Table
  #define RST_PID                 0x13
  #define RST_TABLE_ID            0x71
  #define RST_HEADER_SIZE         PSI_HEADER_SIZE
  #define RST_STATUS_SIZE         9

  //{{{
  inline void rst_init(uint8_t *p_rst)
  {
      psi_set_tableid(p_rst, RST_TABLE_ID);
      psi_init(p_rst, false);
  }
  //}}}
  //{{{
  inline void rst_set_length(uint8_t *p_rst, uint16_t i_rst_length)
  {
      psi_set_length(p_rst, i_rst_length & 0x3fff);
  }
  //}}}
  //{{{
  inline void rstn_init(uint8_t *p_rst_n)
  {
      p_rst_n[8] = 0xf8;
  }
  //}}}

  //{{{
  inline uint16_t rstn_get_tsid(const uint8_t *p_rst_n)
  {
      return (p_rst_n[0] << 8) | p_rst_n[1];
  }
  //}}}
  //{{{
  inline void rstn_set_tsid(uint8_t *p_rst_n, uint16_t i_tsid)
  {
      p_rst_n[0] = i_tsid >> 8;
      p_rst_n[1] = i_tsid & 0xff;
  }
  //}}}

  //{{{
  inline uint16_t rstn_get_onid(const uint8_t *p_rst_n)
  {
      return (p_rst_n[2] << 8) | p_rst_n[3];
  }
  //}}}
  //{{{
  inline void rstn_set_onid(uint8_t *p_rst_n, uint16_t i_onid)
  {
      p_rst_n[2] = i_onid >> 8;
      p_rst_n[3] = i_onid & 0xff;
  }
  //}}}

  //{{{
  inline uint16_t rstn_get_service_id(const uint8_t *p_rst_n)
  {
      return (p_rst_n[4] << 8) | p_rst_n[5];
  }
  //}}}
  //{{{
  inline void rstn_set_service_id(uint8_t *p_rst_n, uint16_t i_service_id)
  {
      p_rst_n[4] = i_service_id >> 8;
      p_rst_n[5] = i_service_id & 0xff;
  }
  //}}}

  //{{{
  inline uint16_t rstn_get_event_id(const uint8_t *p_rst_n)
  {
      return (p_rst_n[6] << 8) | p_rst_n[7];
  }
  //}}}
  //{{{
  inline void rstn_set_event_id(uint8_t *p_rst_n, uint16_t i_event_id)
  {
      p_rst_n[6] = i_event_id >> 8;
      p_rst_n[7] = i_event_id & 0xff;
  }
  //}}}

  //{{{
  inline uint8_t rstn_get_running(const uint8_t *p_rst_n)
  {
      return p_rst_n[8] & 0x07;
  }
  //}}}
  //{{{
  inline void rstn_set_running(uint8_t *p_rst_n, uint8_t i_running_status)
  {
      p_rst_n[8] = 0xf8 | (i_running_status & 0x07);
  }
  //}}}

  //{{{
  inline uint8_t *rst_get_status(uint8_t *p_rst, uint8_t n)
  {
      uint8_t *p_rst_n = p_rst + RST_HEADER_SIZE + n * RST_STATUS_SIZE;
      if (p_rst_n + RST_STATUS_SIZE - p_rst
           > psi_get_length(p_rst) + PSI_HEADER_SIZE)
          return NULL;
      return p_rst_n;
  }
  //}}}
  //{{{
  inline bool rst_validate(const uint8_t *p_rst)
  {
      if (psi_get_syntax(p_rst) || psi_get_tableid(p_rst) != RST_TABLE_ID)
          return false;
      if (psi_get_length(p_rst) % RST_STATUS_SIZE)
          return false;

      return true;
  }
  //}}}
  //}}}

  //{{{  sdt
  // Service Description Table
  #define SDT_PID                 0x11
  #define SDT_TABLE_ID_ACTUAL     0x42
  #define SDT_TABLE_ID_OTHER      0x46
  #define SDT_HEADER_SIZE         (PSI_HEADER_SIZE_SYNTAX1 + 3)
  #define SDT_SERVICE_SIZE        5

  #define sdt_set_tsid psi_set_tableidext
  #define sdt_get_tsid psi_get_tableidext

  //{{{
  inline void sdt_init(uint8_t *p_sdt, bool b_actual)
  {
      psi_init(p_sdt, true);
      psi_set_tableid(p_sdt, b_actual ? SDT_TABLE_ID_ACTUAL : SDT_TABLE_ID_OTHER);
      p_sdt[10] = 0xff;
  }
  //}}}

  //{{{
  inline void sdt_set_length(uint8_t *p_sdt, uint16_t i_sdt_length)
  {
      psi_set_length(p_sdt, SDT_HEADER_SIZE + PSI_CRC_SIZE - PSI_HEADER_SIZE
                      + i_sdt_length);
  }
  //}}}

  //{{{
  inline void sdt_set_onid(uint8_t *p_sdt, uint16_t i_onid)
  {
      p_sdt[8] = i_onid >> 8;
      p_sdt[9] = i_onid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t sdt_get_onid(const uint8_t *p_sdt)
  {
      return (p_sdt[8] << 8) | p_sdt[9];
  }
  //}}}

  //{{{
  inline void sdtn_init(uint8_t *p_sdt_n)
  {
      p_sdt_n[2] = 0xfc;
      p_sdt_n[3] = 0;
  }
  //}}}

  //{{{
  inline void sdtn_set_sid(uint8_t *p_sdt_n, uint16_t i_sid)
  {
      p_sdt_n[0] = i_sid >> 8;
      p_sdt_n[1] = i_sid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t sdtn_get_sid(const uint8_t *p_sdt_n)
  {
      return (p_sdt_n[0] << 8) | p_sdt_n[1];
  }
  //}}}

  //{{{
  inline void sdtn_set_eitschedule(uint8_t *p_sdt_n)
  {
      p_sdt_n[2] |= 0x2;
  }
  //}}}
  //{{{
  inline bool sdtn_get_eitschedule(const uint8_t *p_sdt_n)
  {
      return !!(p_sdt_n[2] & 0x2);
  }
  //}}}

  //{{{
  inline void sdtn_set_eitpresent(uint8_t *p_sdt_n)
  {
      p_sdt_n[2] |= 0x1;
  }
  //}}}
  //{{{
  inline bool sdtn_get_eitpresent(const uint8_t *p_sdt_n)
  {
      return !!(p_sdt_n[2] & 0x1);
  }
  //}}}

  //{{{
  inline void sdtn_set_running(uint8_t *p_sdt_n, uint8_t i_running)
  {
      p_sdt_n[3] &= 0x1f;
      p_sdt_n[3] |= i_running << 5;
  }
  //}}}
  //{{{
  inline uint8_t sdtn_get_running(const uint8_t *p_sdt_n)
  {
      return p_sdt_n[3] >> 5;
  }
  //}}}

  //{{{
  inline void sdtn_set_ca(uint8_t *p_sdt_n)
  {
      p_sdt_n[3] |= 0x10;
  }
  //}}}
  //{{{
  inline bool sdtn_get_ca(const uint8_t *p_sdt_n)
  {
      return !!(p_sdt_n[3] & 0x10);
  }
  //}}}

  //{{{
  inline void sdtn_set_desclength(uint8_t *p_sdt_n, uint16_t i_length)
  {
      p_sdt_n[3] &= ~0xf;
      p_sdt_n[3] |= (i_length >> 8) & 0xf;
      p_sdt_n[4] = i_length & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t sdtn_get_desclength(const uint8_t *p_sdt_n)
  {
      return ((p_sdt_n[3] & 0xf) << 8) | p_sdt_n[4];
  }
  //}}}

  //{{{
  inline uint8_t *sdtn_get_descs(uint8_t *p_sdt_n)
  {
      return &p_sdt_n[3];
  }
  //}}}

  //{{{
  inline uint8_t *sdt_get_service(uint8_t *p_sdt, uint8_t n)
  {
      uint16_t i_section_size = psi_get_length(p_sdt) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      uint8_t *p_sdt_n = p_sdt + SDT_HEADER_SIZE;

      while (n) {
          if (p_sdt_n + SDT_SERVICE_SIZE - p_sdt > i_section_size) return NULL;
          p_sdt_n += SDT_SERVICE_SIZE + sdtn_get_desclength(p_sdt_n);
          n--;
      }
      if (p_sdt_n - p_sdt >= i_section_size) return NULL;
      return p_sdt_n;
  }
  //}}}
  //{{{
  inline bool sdt_validate_service(const uint8_t *p_sdt, const uint8_t *p_sdt_n, uint16_t i_desclength)
  {
      uint16_t i_section_size = psi_get_length(p_sdt) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      return (p_sdt_n + SDT_SERVICE_SIZE + i_desclength
               <= p_sdt + i_section_size);
  }
  //}}}

  //{{{
  inline bool sdt_validate(const uint8_t *p_sdt)
  {
      uint16_t i_section_size = psi_get_length(p_sdt) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      const uint8_t *p_sdt_n;

      if (!psi_get_syntax(p_sdt)
           || (psi_get_tableid(p_sdt) != SDT_TABLE_ID_ACTUAL
                && psi_get_tableid(p_sdt) != SDT_TABLE_ID_OTHER))
          return false;

      p_sdt_n = p_sdt + SDT_HEADER_SIZE;

      while (p_sdt_n + SDT_SERVICE_SIZE - p_sdt <= i_section_size
              && p_sdt_n + SDT_SERVICE_SIZE + sdtn_get_desclength(p_sdt_n) - p_sdt
                  <= i_section_size) {
          if (!descs_validate(p_sdt_n + 3))
              return false;

          p_sdt_n += SDT_SERVICE_SIZE + sdtn_get_desclength(p_sdt_n);
      }

      return (p_sdt_n - p_sdt == i_section_size);
  }
  //}}}
  //{{{
  inline uint8_t *sdt_table_find_service(uint8_t **pp_sections, uint16_t i_sid)
  {
      uint8_t i_last_section = psi_table_get_lastsection(pp_sections);
      uint8_t i;

      for (i = 0; i <= i_last_section; i++) {
          uint8_t *p_section = psi_table_get_section(pp_sections, i);
          uint8_t *p_service;
          int j = 0;

          while ((p_service = sdt_get_service(p_section, j)) != NULL) {
              if (sdtn_get_sid(p_service) == i_sid)
                  return p_service;
              j++;
          }
      }

      return NULL;
  }
  //}}}
  //{{{
  inline bool sdt_table_validate(uint8_t **pp_sections)
  {
      uint8_t i_last_section = psi_table_get_lastsection(pp_sections);
      uint8_t i;
      uint16_t i_onid;

      for (i = 0; i <= i_last_section; i++) {
          uint8_t *p_section = psi_table_get_section(pp_sections, i);
          uint8_t *p_service;
          int j = 0;

          if (!psi_check_crc(p_section))
              return false;

          if (!i)
              i_onid = sdt_get_onid(p_section);
          else if (sdt_get_onid(p_section) != i_onid)
              return false;

          while ((p_service = sdt_get_service(p_section, j)) != NULL) {
              j++;
              /* check that the service is not already in the table */
              if (sdt_table_find_service(pp_sections, sdtn_get_sid(p_service))
                   != p_service)
                  return false;
          }
      }

      return true;
  }
  //}}}
  //}}}
  //{{{  service descriptor
  #define DESC48_HEADER_SIZE      (DESC_HEADER_SIZE + 1)

  //{{{
  inline void desc48_init(uint8_t *p_desc)
  {
      desc_set_tag(p_desc, 0x48);
  }
  //}}}
  //{{{
  inline void desc48_set_type(uint8_t *p_desc, uint8_t i_type)
  {
      p_desc[2] = i_type;
  }
  //}}}
  //{{{
  inline uint8_t desc48_get_type(const uint8_t *p_desc)
  {
      return p_desc[2];
  }
  //}}}
  //{{{
  inline void desc48_set_provider(uint8_t *p_desc, const uint8_t *p_provider, uint8_t i_length)
  {
      uint8_t *p = p_desc + DESC48_HEADER_SIZE;
      p[0] = i_length;
      memcpy(p + 1, p_provider, i_length);
  }
  //}}}
  //{{{
  inline uint8_t *desc48_get_provider(const uint8_t *p_desc, uint8_t *pi_length)
  {
      uint8_t *p = (uint8_t *)p_desc + DESC48_HEADER_SIZE;
      *pi_length = p[0];
      return p + 1;
  }
  //}}}
  //{{{
  inline void desc48_set_service(uint8_t *p_desc, const uint8_t *p_service, uint8_t i_length)
  {
      uint8_t *p = p_desc + DESC48_HEADER_SIZE + 1 + p_desc[3];
      p[0] = i_length;
      memcpy(p + 1, p_service, i_length);
  }
  //}}}
  //{{{
  inline uint8_t *desc48_get_service(const uint8_t *p_desc, uint8_t *pi_length)
  {
      uint8_t *p = (uint8_t *)p_desc + DESC48_HEADER_SIZE + 1 + p_desc[3];
      *pi_length = p[0];
      return p + 1;
  }
  //}}}
  //{{{
  inline bool desc48_validate(const uint8_t *p_desc)
  {
      uint8_t i_length = desc_get_length(p_desc);
      const uint8_t *p = p_desc + DESC48_HEADER_SIZE;

      p += *p + 1;
      if (DESC48_HEADER_SIZE + 2 > i_length + DESC_HEADER_SIZE ||
          p + 1 - p_desc > i_length + DESC_HEADER_SIZE)
          return false;

      p += *p + 1;
      if (p - p_desc > i_length + DESC_HEADER_SIZE)
          return false;

      return true;
  }
  //}}}
  //}}}

  //{{{  nit
  #define NIT_PID                 0x10
  #define NIT_TABLE_ID_ACTUAL     0x40
  #define NIT_TABLE_ID_OTHER      0x41
  #define NIT_HEADER_SIZE         (PSI_HEADER_SIZE_SYNTAX1 + 2)
  #define NIT_HEADER2_SIZE        2
  #define NIT_TS_SIZE             6

  #define nit_set_nid psi_set_tableidext
  #define nit_get_nid psi_get_tableidext

  //{{{
  inline void nit_init(uint8_t *p_nit, bool b_actual)
  {
      psi_init(p_nit, true);
      psi_set_tableid(p_nit, b_actual ? NIT_TABLE_ID_ACTUAL : NIT_TABLE_ID_OTHER);
      p_nit[8] = 0xf0;
  }
  //}}}
  //{{{
  inline void nit_set_length(uint8_t *p_nit, uint16_t i_nit_length)
  {
      psi_set_length(p_nit, NIT_HEADER_SIZE + PSI_CRC_SIZE - PSI_HEADER_SIZE
                      + i_nit_length);
  }
  //}}}
  //{{{
  inline void nit_set_desclength(uint8_t *p_nit, uint16_t i_length)
  {
      p_nit[8] &= ~0xf;
      p_nit[8] |= i_length >> 8;
      p_nit[9] = i_length & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t nit_get_desclength(const uint8_t *p_nit)
  {
      return ((p_nit[8] & 0xf) << 8) | p_nit[9];
  }
  //}}}
  //{{{
  inline uint8_t *nit_get_descs(uint8_t *p_nit)
  {
      return &p_nit[8];
  }
  //}}}
  //{{{
  inline void nith_init(uint8_t *p_nit_h)
  {
      p_nit_h[0] = 0xf0;
  }
  //}}}
  //{{{
  inline void nith_set_tslength(uint8_t *p_nit_h, uint16_t i_length)
  {
      p_nit_h[0] &= ~0xf;
      p_nit_h[0] |= i_length >> 8;
      p_nit_h[1] = i_length & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t nith_get_tslength(const uint8_t *p_nit_h)
  {
      return ((p_nit_h[0] & 0xf) << 8) | p_nit_h[1];
  }
  //}}}
  //{{{
  inline void nitn_init(uint8_t *p_nit_n)
  {
      p_nit_n[4] = 0xf0;
  }
  //}}}
  //{{{
  inline void nitn_set_tsid(uint8_t *p_nit_n, uint16_t i_tsid)
  {
      p_nit_n[0] = i_tsid >> 8;
      p_nit_n[1] = i_tsid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t nitn_get_tsid(const uint8_t *p_nit_n)
  {
      return (p_nit_n[0] << 8) | p_nit_n[1];
  }
  //}}}
  //{{{
  inline void nitn_set_onid(uint8_t *p_nit_n, uint16_t i_onid)
  {
      p_nit_n[2] = i_onid >> 8;
      p_nit_n[3] = i_onid & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t nitn_get_onid(const uint8_t *p_nit_n)
  {
      return (p_nit_n[2] << 8) | p_nit_n[3];
  }
  //}}}
  //{{{
  inline void nitn_set_desclength(uint8_t *p_nit_n, uint16_t i_length)
  {
      p_nit_n[4] &= ~0xf;
      p_nit_n[4] |= i_length >> 8;
      p_nit_n[5] = i_length & 0xff;
  }
  //}}}
  //{{{
  inline uint16_t nitn_get_desclength(const uint8_t *p_nit_n)
  {
      return ((p_nit_n[4] & 0xf) << 8) | p_nit_n[5];
  }
  //}}}
  //{{{
  inline uint8_t *nitn_get_descs(uint8_t *p_nit_n)
  {
      return &p_nit_n[4];
  }
  //}}}
  //{{{
  inline uint8_t *nit_get_header2(uint8_t *p_nit)
  {
      return p_nit + NIT_HEADER_SIZE + nit_get_desclength(p_nit);
  }
  //}}}
  //{{{
  inline uint8_t *nit_get_ts(uint8_t *p_nit, uint8_t n)
  {
      uint16_t i_section_size = psi_get_length(p_nit) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      uint8_t *p_nit_n = p_nit + NIT_HEADER_SIZE + nit_get_desclength(p_nit)
                          + NIT_HEADER2_SIZE;
      if (p_nit_n - p_nit > i_section_size) return NULL;

      while (n) {
          if (p_nit_n + NIT_TS_SIZE - p_nit > i_section_size) return NULL;
          p_nit_n += NIT_TS_SIZE + nitn_get_desclength(p_nit_n);
          n--;
      }
      if (p_nit_n - p_nit >= i_section_size) return NULL;
      return p_nit_n;
  }
  //}}}
  //{{{
  inline bool nit_validate_ts(const uint8_t *p_nit, const uint8_t *p_nit_n, uint16_t i_desclength)
  {
      uint16_t i_section_size = psi_get_length(p_nit) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      return (p_nit_n + NIT_TS_SIZE + i_desclength
               <= p_nit + i_section_size);
  }
  //}}}
  //{{{
  inline bool nit_validate(const uint8_t *p_nit)
  {
      uint16_t i_section_size = psi_get_length(p_nit) + PSI_HEADER_SIZE
                                 - PSI_CRC_SIZE;
      const uint8_t *p_nit_n;

      if (!psi_get_syntax(p_nit)
           || (psi_get_tableid(p_nit) != NIT_TABLE_ID_ACTUAL
                && psi_get_tableid(p_nit) != NIT_TABLE_ID_OTHER))
          return false;

      if (i_section_size < NIT_HEADER_SIZE
           || i_section_size < NIT_HEADER_SIZE + nit_get_desclength(p_nit))
          return false;

      if (!descs_validate(p_nit + 8))
          return false;

      p_nit_n = p_nit + NIT_HEADER_SIZE + nit_get_desclength(p_nit);

      if (nith_get_tslength(p_nit_n) != p_nit + i_section_size - p_nit_n
           - NIT_HEADER2_SIZE)
          return false;
      p_nit_n += NIT_HEADER2_SIZE;

      while (p_nit_n + NIT_TS_SIZE - p_nit <= i_section_size
              && p_nit_n + NIT_TS_SIZE + nitn_get_desclength(p_nit_n) - p_nit
                  <= i_section_size) {
          if (!descs_validate(p_nit_n + 4))
              return false;

          p_nit_n += NIT_TS_SIZE + nitn_get_desclength(p_nit_n);
      }

      return (p_nit_n - p_nit == i_section_size);
  }
  //}}}
  //{{{
  inline uint8_t *nit_table_find_ts(uint8_t **pp_sections, uint16_t i_tsid, uint16_t i_onid)
  {
      uint8_t i_last_section = psi_table_get_lastsection(pp_sections);
      uint8_t i;

      for (i = 0; i <= i_last_section; i++) {
          uint8_t *p_section = psi_table_get_section(pp_sections, i);
          uint8_t *p_ts;
          int j = 0;

          while ((p_ts = nit_get_ts(p_section, j)) != NULL) {
              j++;
              if (nitn_get_tsid(p_ts) == i_tsid && nitn_get_onid(p_ts) == i_onid)
                  return p_ts;
          }
      }

      return NULL;
  }
  //}}}
  //{{{
  inline bool nit_table_validate(uint8_t **pp_sections)
  {
      uint8_t i_last_section = psi_table_get_lastsection(pp_sections);
      uint8_t i;

      for (i = 0; i <= i_last_section; i++) {
          uint8_t *p_section = psi_table_get_section(pp_sections, i);
          uint8_t *p_ts;
          int j = 0;

          if (!psi_check_crc(p_section))
              return false;

          while ((p_ts = nit_get_ts(p_section, j)) != NULL) {
              j++;
              /* check that the TS is not already in the table */
              if (nit_table_find_ts(pp_sections, nitn_get_tsid(p_ts),
                                    nitn_get_onid(p_ts)) != p_ts)
                  return false;
          }
      }

      return true;
  }
  //}}}
  //}}}
  //{{{  network name descriptor
  #define DESC40_HEADER_SIZE      DESC_HEADER_SIZE

  //{{{
  inline void desc40_init(uint8_t *p_desc)
  {
      desc_set_tag(p_desc, 0x40);
  }
  //}}}
  //{{{
  inline void desc40_set_networkname(uint8_t *p_desc, const uint8_t *p_network_name, uint8_t i_length)
  {
      desc_set_length(p_desc, i_length);
      memcpy(p_desc + 2, p_network_name, i_length);
  }
  //}}}
  //}}}

  constexpr int PADDING_PID = 8191;
  constexpr int MAX_PIDS = 8192;
  constexpr int UNUSED_PID = MAX_PIDS + 1;

  constexpr int MIN_SECTION_FRAGMENT = PSI_HEADER_SIZE_SYNTAX1;

  // EIT is carried in several separate tables, we need to track each table
  // separately, otherwise one table overwrites sections of another table
  constexpr int MAX_EIT_TABLES = EIT_TABLE_ID_SCHED_ACTUAL_LAST - EIT_TABLE_ID_PF_ACTUAL;
  //}}}
  //{{{
  uint8_t kPadTs[188] = {
    0x47, 0x1f, 0xff, 0x10, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
  //}}}
  //{{{
  struct sRtpPacket {
    sRtpPacket* mNextPacket;
    int64_t mDts;
    int mDepth;
    cTsBlock* mBlocks[];
    };
  //}}}
  //{{{
  struct sEitSections {
    PSI_TABLE_DECLARE(data);
    };
  //}}}
  //{{{
  struct sSid {
    uint16_t mSid;
    uint16_t mPmtPid;
    uint8_t* mCurrentPmt;
    sEitSections mEitTables[MAX_EIT_TABLES];
    };
  //}}}
  //{{{
  class cOutputConfig {
  public:
    static constexpr int DEFAULT_OUTPUT_LATENCY = 200000; // 200ms
    static constexpr int DEFAULT_MAX_RETENTION = 40000;   //  40ms

    //{{{
    void initialise() {

      mOutputDvb = true;
      mOutputEpg = true;

      mFamily = AF_UNSPEC;
      mIndexV6 = -1;
      mConnectAddr.ss_family = AF_UNSPEC;
      mBindAddr.ss_family = AF_UNSPEC;

      mNetworkId = 0xffff;

      mSsrc[0] = 0;
      mSsrc[1] = 0;
      mSsrc[2] = 0;
      mSsrc[3] = 0;
      mTtl = 64;
      mTos = 0;
      mMtu = 0;

      mTsId = -1;
      mSid = 0;

      mPids = NULL;
      mNumPids = 0;

      mNewSid = 0;
      mOnid = 0;
      }
    //}}}
    //{{{
    void initialise (const string& networkName, const string& providerName) {

      mDisplayName = "displayName";
      mOutputDvb = true;
      mOutputEpg = true;

      mFamily = AF_UNSPEC;
      mIndexV6 = -1;
      mConnectAddr.ss_family = AF_UNSPEC;
      mBindAddr.ss_family = AF_UNSPEC;

      mNetworkId = 0xffff;
      mNetworkName = networkName;
      mServiceName = "";
      mProviderName = providerName;

      mSsrc[0] = 0;
      mSsrc[1] = 0;
      mSsrc[2] = 0;
      mSsrc[3] = 0;
      mTtl = 64;
      mTos = 0;
      mMtu = 0;

      mTsId = -1;
      mSid = 0;

      mPids = NULL;
      mNumPids = 0;

      mNewSid = 0;
      mOnid = 0;
      }
    //}}}

    string mDisplayName;
    uint64_t mOutputDvb = true;
    uint64_t mOutputEpg = true;

    // identity
    int mFamily = AF_UNSPEC;
    int mIndexV6 = -1;
    struct sockaddr_storage mConnectAddr;
    struct sockaddr_storage mBindAddr;

    // output config
    uint16_t mNetworkId = 0xffff;
    string mNetworkName = "";
    string mServiceName = "";
    string mProviderName = "";

    uint8_t mSsrc[4] = { 0 };
    int mTtl = 64;
    uint8_t mTos = 0;
    int mMtu = 0;

    // demux config
    int mTsId = -1;
    uint16_t mSid = 0; // 0 if raw mode

    int mNumPids = 0;
    uint16_t* mPids = NULL;

    uint16_t mNewSid = 0;
    uint16_t mOnid = 0;
    };
  //}}}
  //{{{
  class cOutput {
  public:
    //{{{
    bool initialise (const cOutputConfig* outputConfig) {

      socklen_t sockaddrLen =
        (outputConfig->mFamily == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

      mConfig.initialise();

      mSocket = 0;
      mPackets = NULL;
      mLastPacket = NULL;
      mPacketLifo = NULL;
      mPacketCount = 0;
      mSeqnum = rand() & 0xffff;

      mPatVersion = rand() & 0xff;
      mPatContinuity = rand() & 0xf;
      mPatSection = NULL;

      mPmtVersion = rand() & 0xff;
      mPmtContinuity = rand() & 0xf;
      mPmtSection = NULL;

      mNitVersion = rand() & 0xff;
      mNitContinuity = rand() & 0xf;
      mNitSection = NULL;

      mSdtVersion = rand() & 0xff;
      mSdtContinuity = rand() & 0xf;
      mSdtSection = NULL;

      mEitTsBuffer = NULL;
      mEit_ts_buffer_offset = 0;
      mEitContinuity = rand() & 0xf;

      mTsId = 0;
      mPcrPid = 0;
      for (int i = 0; i < MAX_PIDS; i++) {
        mNewPids[i] = UNUSED_PID;
        mFreePids[i] = UNUSED_PID;
        }
      //raw_pkt_header;

      // init socket-related fields
      mConfig.mFamily = outputConfig->mFamily;
      memcpy (&mConfig.mConnectAddr, &outputConfig->mConnectAddr, sizeof(struct sockaddr_storage));
      memcpy (&mConfig.mBindAddr, &outputConfig->mBindAddr, sizeof(struct sockaddr_storage));
      mConfig.mIndexV6 = outputConfig->mIndexV6;

      mSocket = socket (outputConfig->mFamily, SOCK_DGRAM, IPPROTO_UDP);
      if (mSocket < 0) {
        //{{{  return error
        cLog::log (LOGERROR, "couldn't create socket %s", strerror (errno));
        return false;
        }
        //}}}

      int ret = 0;
      if (outputConfig->mBindAddr.ss_family != AF_UNSPEC) {
        //{{{  bind socket, set multicast
        if (bind (mSocket, (struct sockaddr*)&outputConfig->mBindAddr, sockaddrLen) < 0)
          cLog::log (LOGINFO, "couldn't bind socket %s", strerror (errno));

        if (outputConfig->mFamily == AF_INET) {
          struct sockaddr_in* connectAddr = (struct sockaddr_in*)&mConfig.mConnectAddr;
          struct sockaddr_in* bindAddr = (struct sockaddr_in*)&mConfig.mBindAddr;

          if (IN_MULTICAST(ntohl (connectAddr->sin_addr.s_addr)))
            ret = setsockopt (mSocket, IPPROTO_IP, IP_MULTICAST_IF,
                              (void*)&bindAddr->sin_addr.s_addr, sizeof(bindAddr->sin_addr.s_addr));
          }
        }
        //}}}
      if (outputConfig->mFamily == AF_INET6 && outputConfig->mIndexV6 != -1) {
        //{{{  set ipv6 stuff
        struct sockaddr_in6* p_addr = (struct sockaddr_in6*)&mConfig.mConnectAddr;
        if (IN6_IS_ADDR_MULTICAST(&p_addr->sin6_addr))
          ret = setsockopt (mSocket, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                            (void*)&outputConfig->mIndexV6, sizeof(outputConfig->mIndexV6));
        }
        //}}}
      if (ret == -1)
        cLog::log (LOGINFO, "couldn't join multicast address %s", strerror (errno));
      if (connect (mSocket, (struct sockaddr*)&mConfig.mConnectAddr, sockaddrLen) < 0) {
        //{{{  return error
        cLog::log (LOGERROR, "couldn't connect socket %s", strerror (errno));
        close (mSocket);
        return false;
        }
        //}}}
      return true;
      }
    //}}}

    int getBlockCount() { return (mConfig.mMtu - kRtpHeaderSize) / 188; }

    //{{{
    sRtpPacket* packetNew() {

      sRtpPacket* packet;

      if (mPacketCount) {
        packet = mPacketLifo;
        mPacketLifo = packet->mNextPacket;
        mPacketCount--;
        }
      else
        packet = (sRtpPacket*)malloc (sizeof(sRtpPacket) + getBlockCount() * sizeof(cTsBlock*));

      packet->mDepth = 0;
      packet->mNextPacket = NULL;

      return packet;
      }
    //}}}
    //{{{
    void packetDelete (sRtpPacket* packet) {

      if (mPacketCount >= kMaxOutputPackets) {
        free (packet);
        return;
        }

      packet->mNextPacket = mPacketLifo;
      mPacketLifo = packet;
      mPacketCount++;
      }
    //}}}
    //{{{
    void packetCleanup() {

      while (mPacketCount) {
        sRtpPacket* packet = mPacketLifo;
        mPacketLifo = packet->mNextPacket;
        free (packet);

        mPacketCount--;
        }
      }
    //}}}

    // vars
    cOutputConfig mConfig;

    // output
    int mSocket;

    sRtpPacket* mLastPacket;
    sRtpPacket* mPacketLifo;
    sRtpPacket* mPackets;

    uint16_t mSeqnum;

    // demux
    uint8_t mPatVersion;
    uint8_t mPatContinuity;
    uint8_t* mPatSection;

    uint8_t mPmtVersion;
    uint8_t mPmtContinuity;
    uint8_t* mPmtSection;

    uint8_t mNitVersion;
    uint8_t mNitContinuity;
    uint8_t* mNitSection;

    uint8_t mSdtVersion;
    uint8_t mSdtContinuity;
    uint8_t* mSdtSection;

    uint8_t mEitContinuity;
    uint8_t mEit_ts_buffer_offset;
    cTsBlock* mEitTsBuffer;

    uint16_t mTsId;

    // incomplete PID (only PCR packets)
    uint16_t mPcrPid;

    // Arrays used for mapping pids, newpids is indexed using the original pid
    uint16_t mNewPids[MAX_PIDS];
    uint16_t mFreePids[MAX_PIDS];   // used where multiple streams of the same type are used

  private:
    static constexpr int kMaxOutputPackets = 100;

    unsigned int mPacketCount;
    };
  //}}}
  //{{{
  struct sTsPid {
    int mRefCount;
    int mPsiRefCount;

    int mDemuxFd;

    bool mPes;
    int8_t mLastContinuity;

    // biTStream PSI section gathering
    uint16_t mPsiBufferUsed;
    uint8_t* mPsiBuffer;

    int mNumOutputs;
    cOutput** mOutputs;
    };
  //}}}
  //{{{  vars
  cDvb* mDvb;
  cTsBlockPool* mBlockPool;

  // time
  int64_t mWallclock = 0;
  int64_t mLastDts = -1;

  // tuner
  bool mBudgetMode = false;
  int mBudgetDemuxFd;

  uint64_t mNumPackets = 0;
  uint64_t mNumInvalids = 0;
  uint64_t mNumDiscontinuities = 0;
  uint64_t mNumErrors = 0;
  int mTunerErrors = 0;

  // pids
  sTsPid mTsPids[MAX_PIDS];

  // sids
  int mNumSids = 0;
  sSid** mSids = NULL;

  // tables
  PSI_TABLE_DECLARE(mCurrentPatSections);
  PSI_TABLE_DECLARE(mNextPatSections);
  PSI_TABLE_DECLARE(mCurrentCatSections);
  PSI_TABLE_DECLARE(mNextCatSections);
  PSI_TABLE_DECLARE(mCurrentNitSections);
  PSI_TABLE_DECLARE(mNexttNitSections);
  PSI_TABLE_DECLARE(mCurrentSdtSections);
  PSI_TABLE_DECLARE(mNextSdtSections);

  // outputs
  vector <cOutput*> mOutputs;
  //}}}

  //{{{
  int64_t mdate() {
  // use POSIX monotonic clock if available
  // - run-time fallback to real-time clock

    struct timespec ts;
    if (clock_gettime (CLOCK_MONOTONIC, &ts) == EINVAL)
      (void)clock_gettime (CLOCK_REALTIME, &ts);

    return ((int64_t)ts.tv_sec * (int64_t)1000000) + (int64_t)(ts.tv_nsec / 1000);
    }
  //}}}
  //{{{  demux utils
  //{{{
  bool isIn (const uint16_t* pids, int numPids, uint16_t pidNum) {

    for (int i = 0; i < numPids; i++)
      if (pidNum == pids[i])
        return true;

    return false;
    }
  //}}}
  //{{{
  bool pidCarriesPES (const uint8_t* es) {

    uint8_t i_type = pmtn_get_streamtype (es);
    switch (i_type) {
      case 0x1: // video MPEG-1
      case 0x2: // video
      case 0x3: // audio MPEG-1
      case 0x4: // audio
      case 0x6: // private PES data
      case 0xf: // audio AAC
      case 0x10: // video MPEG-4
      case 0x11: // audio AAC LATM
      case 0x1b: // video H264
      case 0x24: // video H265
      case 0x81: // ATSC A/52
      case 0x87: // ATSC Enhanced A/52
        return true;
        break;

      default:
        return false;
        break;
      }
    }
  //}}}
  //{{{
  bool pidWouldBeSelected (uint8_t* es) {

    uint8_t i_type = pmtn_get_streamtype (es);
    switch (i_type) {
      case 0x1: // video MPEG-1
      case 0x2: // video
      case 0x3: // audio MPEG-1
      case 0x4: // audio
      case 0xf: // audio AAC ADTS
      case 0x10: // video MPEG-4
      case 0x11: // audio AAC LATM
      case 0x1b: // video H264
      case 0x24: // video H265
      case 0x81: // ATSC A/52
      case 0x87: // ATSC Enhanced A/52
        return true;
        break;

      case 0x6: {
        uint16_t j = 0;
        const uint8_t* desc;
        while ((desc = descs_get_desc (pmtn_get_descs (es), j))) {
          uint8_t i_tag = desc_get_tag (desc);
          j++;

          if (i_tag == 0x46 // VBI + teletext
              || i_tag == 0x56 // teletext
              || i_tag == 0x59 // dvbsub
              || i_tag == 0x6a // A/52
              || i_tag == 0x7a // Enhanced A/52
              || i_tag == 0x7b // DCA
              || i_tag == 0x7c // AAC
              )
            return true;
          }
        break;
        }

      default:
        break;
      }

    // FIXME: also parse IOD
    return false;
    }
  //}}}

  //{{{
  const char* h222_stream_type_desc (uint8_t streamType) {
  // See ISO/IEC 13818-1 : 2000 (E) | Table 2-29 - Stream type assignments, Page 66 (48)

    switch (streamType) {
      case 0x00: return "Reserved stream";
      case 0x01: return "11172-2 video (MPEG-1)";
      case 0x02: return "H.262/13818-2 video (MPEG-2) or 11172-2 constrained video";
      case 0x03: return "11172-3 audio (MPEG-1)";
      case 0x04: return "13818-3 audio (MPEG-2)";
      case 0x05: return "H.222.0/13818-1  private sections";
      case 0x06: return "H.222.0/13818-1 PES private data";
      case 0x07: return "13522 MHEG";
      case 0x08: return "H.222.0/13818-1 Annex A - DSM CC";
      case 0x09: return "H.222.1";
      case 0x0A: return "13818-6 type A";
      case 0x0B: return "13818-6 type B";
      case 0x0C: return "13818-6 type C";
      case 0x0D: return "13818-6 type D";
      case 0x0E: return "H.222.0/13818-1 auxiliary";
      case 0x0F: return "13818-7 Audio with ADTS transport syntax";
      case 0x10: return "14496-2 Visual (MPEG-4 part 2 video)";
      case 0x11: return "14496-3 Audio with LATM transport syntax (14496-3/AMD 1)";
      case 0x12: return "14496-1 SL-packetized or FlexMux stream in PES packets";
      case 0x13: return "14496-1 SL-packetized or FlexMux stream in 14496 sections";
      case 0x14: return "ISO/IEC 13818-6 Synchronized Download Protocol";
      case 0x15: return "Metadata in PES packets";
      case 0x16: return "Metadata in metadata sections";
      case 0x17: return "Metadata in 13818-6 Data Carousel";
      case 0x18: return "Metadata in 13818-6 Object Carousel";
      case 0x19: return "Metadata in 13818-6 Synchronized Download Protocol";
      case 0x1A: return "13818-11 MPEG-2 IPMP stream";
      case 0x1B: return "H.264/14496-10 video (MPEG-4/AVC)";
      case 0x24: return "H.265/23008-2 video (HEVC)";
      case 0x42: return "AVS Video";
      case 0x7F: return "IPMP stream";
      default  : return "Unknown stream";
      }
    }
  //}}}
  //{{{
  const char* getPidDesc (uint16_t pidNum, uint16_t* sidNum) {

    // Simple cases
    switch (pidNum) {
      case 0x00: return "PAT";
      case 0x01: return "CAT";
      case 0x11: return "SDT";
      case 0x12: return "EPG";
      case 0x14: return "TDT/TOT";
      }

    // Detect NIT pid
    int j;
    uint8_t lastSection;
    uint16_t nitPid = NIT_PID;
    if (psi_table_validate (mCurrentPatSections)) {
      lastSection = psi_table_get_lastsection (mCurrentPatSections);
      for (int i = 0; i <= lastSection; i++) {
        uint8_t* section = psi_table_get_section (mCurrentPatSections, i);
        uint8_t* program;
        j = 0;
        while ((program = pat_get_program (section, j++))) {
          // Programs with PID == 0 are actually NIT
          if (patn_get_program (program) == 0) {
            nitPid = patn_get_pid (program);
            break;
            }
          }
        }
      }

    // Detect streams in PMT
    uint16_t pcrPid = 0;
    for (int k = 0; k < mNumSids; k++) {
      sSid* sid = mSids[k];
      if (sid->mPmtPid == pidNum) {
        if (sidNum)
          *sidNum = sid->mSid;
        return "PMT";
        }

      if (sid->mSid && sid->mCurrentPmt) {
        uint8_t* mCurrentPmt = sid->mCurrentPmt;
        uint8_t* p_current_es;

        // The PCR PID can be alone or PCR can be carried in some other PIDs (mostly video)
        // so just remember the pid and if it is alone it will be reported as PCR, otherwise
        // stream type of the PID will be reported
        if (pidNum == pmt_get_pcrpid (mCurrentPmt)) {
          if (sidNum)
            *sidNum = sid->mSid;
          pcrPid = pmt_get_pcrpid (mCurrentPmt);
          }

        // Detect stream types
        j = 0;
        while ((p_current_es = pmt_get_es (mCurrentPmt, j++))) {
          if (pmtn_get_pid (p_current_es) == pidNum) {
            if (sidNum)
              *sidNum = sid->mSid;
            return h222_stream_type_desc (pmtn_get_streamtype (p_current_es));
            }
          }
        }
      }

    // Are there any other PIDs?
    if (pidNum == nitPid)
      return "NIT";
    if (pidNum == pcrPid)
      return "PCR";
    return "...";
    }
  //}}}

  //{{{
  sSid* findSid (int16_t sidNum) {

    for (int i = 0; i < mNumSids; i++) {
      sSid* sid = mSids[i];
      if (sid->mSid == sidNum)
        return sid;
      }
    return NULL;
    }
  //}}}
  //{{{
  void getPids (uint16_t** ppi_wanted_pids, int* pi_num_wanted_pids,
                uint16_t* pi_wanted_pcr_pid, uint16_t sidNum,
                const uint16_t* pids, int numPids) {

    *pi_wanted_pcr_pid = 0;
    if (numPids || sidNum == 0) {
      *pi_num_wanted_pids = numPids;
      *ppi_wanted_pids = (uint16_t*)malloc (sizeof(uint16_t) * numPids);
      memcpy (*ppi_wanted_pids, pids, sizeof(uint16_t) * numPids);
      if (sidNum == 0)
        return;
      }
    else {
      *pi_num_wanted_pids = 0;
      *ppi_wanted_pids = NULL;
      }

    sSid* sid = findSid (sidNum);
    if (sid == NULL)
      return;

    uint8_t* pmt = sid->mCurrentPmt;
    uint16_t pmtPid = sid->mPmtPid;
    if (pmt == NULL) {
      cLog::log (LOGINFO, "no current PMT on sid %d", sidNum);
      return;
      }

    uint16_t pcrPid = pmt_get_pcrpid (pmt);
    uint8_t j = 0;
    uint8_t* es;
    while ((es = pmt_get_es (pmt, j))) {
      j++;
      uint16_t pidNum = pmtn_get_pid (es);
      bool b_select;
      if (numPids)
        b_select = isIn (pids, numPids, pidNum);
      else {
        b_select = pidWouldBeSelected (es);
        if (b_select) {
          *ppi_wanted_pids = (uint16_t*)realloc (*ppi_wanted_pids, (*pi_num_wanted_pids + 1) * sizeof(uint16_t));
          (*ppi_wanted_pids)[(*pi_num_wanted_pids)++] = pidNum;
          }
        }
      }

    if (pcrPid != PADDING_PID
        && pcrPid != pmtPid
        && !isIn (*ppi_wanted_pids, *pi_num_wanted_pids, pcrPid)) {
      *ppi_wanted_pids = (uint16_t*)realloc (*ppi_wanted_pids, (*pi_num_wanted_pids + 1) * sizeof(uint16_t));
      (*ppi_wanted_pids)[(*pi_num_wanted_pids)++] = pcrPid;

      // We only need the PCR packets of this stream (incomplete)
      *pi_wanted_pcr_pid = pcrPid;
      cLog::log (LOGINFO, "Requesting partial PCR PID %d", pcrPid);
      }

    }
  //}}}

  //{{{
  void setPid (uint16_t pidNum) {

    mTsPids[pidNum].mRefCount++;

    if (!mBudgetMode
        && mTsPids[pidNum].mRefCount
        && mTsPids[pidNum].mDemuxFd == -1)
      mTsPids[pidNum].mDemuxFd = mDvb->setFilter (pidNum);
    }
  //}}}
  //{{{
  void unsetPid (uint16_t pidNum) {

    mTsPids[pidNum].mRefCount--;

    if (!mBudgetMode
        && !mTsPids[pidNum].mRefCount
        && mTsPids[pidNum].mDemuxFd != -1) {
      mDvb->unsetFilter (mTsPids[pidNum].mDemuxFd, pidNum);
      mTsPids[pidNum].mDemuxFd = -1;
      }
    }
  //}}}
  //{{{
  void startPid (cOutput* output, uint16_t pidNum) {

    int j;
    for (j = 0; j < mTsPids[pidNum].mNumOutputs; j++)
      if (mTsPids[pidNum].mOutputs[j] == output)
        break;

    if (j == mTsPids[pidNum].mNumOutputs) {
      for (j = 0; j < mTsPids[pidNum].mNumOutputs; j++)
        if (mTsPids[pidNum].mOutputs[j] == NULL)
          break;

      if (j == mTsPids[pidNum].mNumOutputs) {
        mTsPids[pidNum].mNumOutputs++;
        mTsPids[pidNum].mOutputs =
          (cOutput**)realloc (mTsPids[pidNum].mOutputs, sizeof(cOutput*) * mTsPids[pidNum].mNumOutputs);
        }

      mTsPids[pidNum].mOutputs[j] = output;
      setPid (pidNum);
      }
    }
  //}}}
  //{{{
  void stopPid (cOutput* output, uint16_t pidNum) {

    int j;
    for (j = 0; j < mTsPids[pidNum].mNumOutputs; j++)
      if (mTsPids[pidNum].mOutputs[j])
        if (mTsPids[pidNum].mOutputs[j] == output)
          break;

    if (j != mTsPids[pidNum].mNumOutputs) {
      mTsPids[pidNum].mOutputs[j] = NULL;
      unsetPid (pidNum);
      }
    }
  //}}}
  //{{{
  void selectPid (uint16_t sidNum, uint16_t pidNum, bool pcr) {

    for (auto output : mOutputs)
      if (output->mConfig.mSid == sidNum) {
        if (output->mConfig.mNumPids &&
            !isIn (output->mConfig.mPids, output->mConfig.mNumPids, pidNum)) {
          if (pcr)
            output->mPcrPid = pidNum;
          else
            continue;
          }
        startPid (output, pidNum);
        }
    }
  //}}}
  //{{{
  void unselectPid (uint16_t sidNum, uint16_t pidNum) {

    for (auto output : mOutputs)
      if ((output->mConfig.mSid == sidNum) && !output->mConfig.mNumPids)
        stopPid (output, pidNum);
    }
  //}}}

  //{{{
  void selectPMT (uint16_t sidNum, uint16_t pidNum) {

    mTsPids[pidNum].mPsiRefCount++;
    mTsPids[pidNum].mPes = false;
    setPid (pidNum);
    }
  //}}}
  //{{{
  void unselectPMT (uint16_t sidNum, uint16_t pidNum) {

    mTsPids[pidNum].mPsiRefCount--;
    if (!mTsPids[pidNum].mPsiRefCount)
      psi_assemble_reset (&mTsPids[pidNum].mPsiBuffer, &mTsPids[pidNum].mPsiBufferUsed);

    unsetPid (pidNum);
    }
  //}}}

  //{{{
  void copyDescriptors (uint8_t* descs, uint8_t* currentDescs) {

    descs_set_length (descs, DESCS_MAX_SIZE);

    uint16_t k = 0;
    uint16_t j = 0;
    const uint8_t* currentDesc;
    while ((currentDesc = descs_get_desc (currentDescs, j))) {
      uint8_t tag = desc_get_tag (currentDesc);
      j++;
      if (tag == 0x9)
        continue;

      uint8_t* desc = descs_get_desc (descs, k);
      if (desc == NULL)
        continue; // This shouldn't happen

      k++;
      memcpy (desc, currentDesc, DESC_HEADER_SIZE + desc_get_length (currentDesc));
      }

    uint8_t* desc = descs_get_desc (descs, k);
    if (desc == NULL)
      // This shouldn't happen if the incoming PMT is valid
      descs_set_length (descs, 0);
    else
      descs_set_length (descs, desc - descs - DESCS_HEADER_SIZE);
    }
  //}}}
  //{{{
  void newPAT (cOutput* output) {

    free (output->mPatSection);

    output->mPatSection = NULL;
    output->mPatVersion++;

    if (!output->mConfig.mSid)
      return;
    if (!psi_table_validate (mCurrentPatSections))
      return;

    const uint8_t* p_program = pat_table_find_program (mCurrentPatSections, output->mConfig.mSid);
    if (p_program == NULL)
      return;

    uint8_t* p = output->mPatSection = psi_allocate();
    pat_init (p);
    psi_set_length (p, PSI_MAX_SIZE);
    pat_set_tsid (p, output->mTsId);
    psi_set_version (p, output->mPatVersion);
    psi_set_current (p);
    psi_set_section (p, 0);
    psi_set_lastsection (p, 0);

    uint8_t k = 0;
    if (output->mConfig.mOutputDvb) {
      // NIT pf
      p = pat_get_program (output->mPatSection, k++);
      patn_init (p);
      patn_set_program (p, 0);
      patn_set_pid (p, NIT_PID);
      }

    p = pat_get_program (output->mPatSection, k++);
    patn_init (p);
    if (output->mConfig.mNewSid) {
      cLog::log (LOGINFO, "mapping pat sid %d to %d", output->mConfig.mSid, output->mConfig.mNewSid);
      patn_set_program (p, output->mConfig.mNewSid);
      }
    else
      patn_set_program (p, output->mConfig.mSid);

    patn_set_pid (p, patn_get_pid (p_program));

    p = pat_get_program (output->mPatSection, k);
    pat_set_length (output->mPatSection,  p - output->mPatSection - PAT_HEADER_SIZE);
    psi_set_crc (output->mPatSection);
    }
  //}}}
  //{{{
  void newPMT (cOutput* output) {

    free (output->mPmtSection);

    output->mPmtSection = NULL;
    output->mPmtVersion++;
    if (!output->mConfig.mSid)
      return;

    sSid* sid = findSid (output->mConfig.mSid);
    if (sid == NULL)
      return;

    if (sid->mCurrentPmt == NULL)
      return;

    uint8_t* currentPmt = sid->mCurrentPmt;
    uint8_t* p = output->mPmtSection = psi_allocate();
    pmt_init (p);
    psi_set_length (p, PSI_MAX_SIZE);
    if (output->mConfig.mNewSid) {
      cLog::log (LOGINFO, "mapping pmt sid %d to %d", output->mConfig.mSid, output->mConfig.mNewSid);
      pmt_set_program (p, output->mConfig.mNewSid);
      }
    else
      pmt_set_program (p, output->mConfig.mSid);

    psi_set_version (p, output->mPmtVersion);
    psi_set_current (p);
    pmt_set_desclength (p, 0);
    for (int i = 0; i < MAX_PIDS; i++) {
      output->mNewPids[i] = UNUSED_PID;
      output->mFreePids[i] = UNUSED_PID;
      }
    copyDescriptors (pmt_get_descs (p), pmt_get_descs (currentPmt));

    uint16_t j = 0;
    uint16_t k = 0;
    uint8_t* currentEs;
    while ((currentEs = pmt_get_es (currentPmt, j))) {
      uint16_t pidNum = pmtn_get_pid (currentEs);
      j++;
      if ((output->mConfig.mNumPids || !pidWouldBeSelected (currentEs))
          && !isIn (output->mConfig.mPids, output->mConfig.mNumPids, pidNum))
        continue;

      uint8_t* es = pmt_get_es (p, k);
      if (es == NULL)
        continue; // This shouldn't happen

      k++;
      pmtn_init (es);
      pmtn_set_streamtype (es, pmtn_get_streamtype (currentEs));
      pmtn_set_pid (es, pidNum);
      pmtn_set_desclength (es, 0);
      copyDescriptors (pmtn_get_descs (es), pmtn_get_descs (currentEs));
      }

    // Do the pcr pid after everything else as it may have been remapped
    uint16_t pcrPid = pmt_get_pcrpid (currentPmt);
    if (output->mNewPids[pcrPid] != UNUSED_PID) {
      cLog::log (LOGINFO, "remap - pcr pid changed from 0x%x %u to 0x%x %u",
                 pcrPid, pcrPid, output->mNewPids[pcrPid], output->mNewPids[pcrPid]);
      pcrPid = output->mNewPids[pcrPid];
      }
    else
      cLog::log (LOGINFO1, "pcr pid kept original value of 0x%x %u", pcrPid, pcrPid);

    pmt_set_pcrpid (p, pcrPid);
    uint8_t* es = pmt_get_es (p, k);
    if (es == NULL)
      // This shouldn't happen if the incoming PMT is valid
      pmt_set_length (p, 0);
    else
      pmt_set_length (p, es - p - PMT_HEADER_SIZE);
    psi_set_crc (p);
    }
  //}}}
  //{{{
  void newNIT (cOutput* output) {

    free (output->mNitSection);

    uint8_t* nitSection = psi_allocate();
    output->mNitSection = nitSection;
    output->mNitVersion++;

    nit_init (nitSection, true);
    nit_set_length (nitSection, PSI_MAX_SIZE);
    nit_set_nid (nitSection, output->mConfig.mNetworkId);
    psi_set_version (nitSection, output->mNitVersion);
    psi_set_current (nitSection);
    psi_set_section (nitSection, 0);
    psi_set_lastsection (nitSection, 0);

    if (output->mConfig.mNetworkName.size()) {
      nit_set_desclength (nitSection, DESCS_MAX_SIZE);
      uint8_t* descs = nit_get_descs (nitSection);
      uint8_t* desc = descs_get_desc (descs, 0);
      desc40_init (desc);
      desc40_set_networkname (desc, (const uint8_t*)output->mConfig.mNetworkName.c_str(),
                                     output->mConfig.mNetworkName.size());
      desc = descs_get_desc (descs, 1);
      descs_set_length (descs, desc - descs - DESCS_HEADER_SIZE);
      }
    else
      nit_set_desclength (nitSection, 0);

    uint8_t* header2 = nit_get_header2 (nitSection);
    nith_init (header2);
    nith_set_tslength (header2, NIT_TS_SIZE);

    uint8_t* ts = nit_get_ts (nitSection, 0);
    nitn_init (ts);
    nitn_set_tsid (ts, output->mTsId);
    if (output->mConfig.mOnid)
      nitn_set_onid (ts, output->mConfig.mOnid);
    else
      nitn_set_onid (ts, output->mConfig.mNetworkId);
    nitn_set_desclength (ts, 0);

    ts = nit_get_ts (nitSection, 1);
    if (ts == NULL)
      // This shouldn't happen
      nit_set_length (nitSection, 0);
    else
      nit_set_length (nitSection, ts - nitSection - NIT_HEADER_SIZE);

    psi_set_crc (output->mNitSection);
    }
  //}}}
  //{{{
  void newSDT (cOutput* output) {

    free (output->mSdtSection);

    output->mSdtSection = NULL;
    output->mSdtVersion++;
    if (!output->mConfig.mSid)
      return;
    if (!psi_table_validate (mCurrentSdtSections))
      return;

    uint8_t* currentService = sdt_table_find_service (mCurrentSdtSections, output->mConfig.mSid);
    if (currentService == NULL) {
      if (output->mPatSection && pat_get_program (output->mPatSection, 0) == NULL) {
        // Empty PAT and no SDT anymore
        free (output->mPatSection);
        output->mPatSection = NULL;
        output->mPatVersion++;
        }
      return;
      }

    uint8_t* sdtSection = psi_allocate();
    output->mSdtSection = sdtSection;
    sdt_init (sdtSection, true);
    sdt_set_length (sdtSection, PSI_MAX_SIZE);
    sdt_set_tsid (sdtSection, output->mTsId);
    psi_set_version (sdtSection, output->mSdtVersion);
    psi_set_current (sdtSection);
    psi_set_section (sdtSection, 0);
    psi_set_lastsection (sdtSection, 0);
    if (output->mConfig.mOnid)
      sdt_set_onid (sdtSection, output->mConfig.mOnid);
    else
      sdt_set_onid (sdtSection, sdt_get_onid (psi_table_get_section (mCurrentSdtSections, 0)));

    uint8_t* service = sdt_get_service (sdtSection, 0);
    sdtn_init (service);
    if (output->mConfig.mNewSid) {
      cLog::log (LOGINFO, "mapping sdt sid %d to %d", output->mConfig.mSid, output->mConfig.mNewSid);
      sdtn_set_sid (service, output->mConfig.mNewSid);
      }
    else
      sdtn_set_sid (service, output->mConfig.mSid);

    // We always forward EITpf
    if (sdtn_get_eitpresent (currentService))
      sdtn_set_eitpresent (service);

    if (output->mConfig.mOutputEpg && sdtn_get_eitschedule (currentService))
      sdtn_set_eitschedule (service);

    sdtn_set_running (service, sdtn_get_running (currentService));

    // Do not set free_ca
    sdtn_set_desclength (service, sdtn_get_desclength (currentService));

    if (!output->mConfig.mProviderName.size() && !output->mConfig.mServiceName.size()) {
        // Copy all descriptors unchanged
        memcpy (descs_get_desc (sdtn_get_descs (service), 0),
                descs_get_desc (sdtn_get_descs (currentService), 0),
                sdtn_get_desclength (currentService));
      }
    else {
      int j = 0;
      int totalDescLen = 0;
      uint8_t* desc;
      uint8_t* newDesc = descs_get_desc (sdtn_get_descs (service), 0);
      while ((desc = descs_get_desc (sdtn_get_descs (currentService), j++))) {
        // Regenerate descriptor 48 (service name)
        if (desc_get_tag (desc) == 0x48 && desc48_validate (desc)) {
          uint8_t oldProviderLen;
          uint8_t oldServiceLen;
          uint8_t newDescLen = 3; // 1 byte - type, 1 byte provider_len, 1 byte service_len
          const uint8_t* oldProvider = desc48_get_provider (desc, &oldProviderLen);
          const uint8_t* oldService = desc48_get_service (desc, &oldServiceLen);

          desc48_init (newDesc);
          desc48_set_type (newDesc, desc48_get_type (desc));

          if (output->mConfig.mProviderName.size()) {
            desc48_set_provider (newDesc, (const uint8_t*)output->mConfig.mProviderName.c_str(),
                                           output->mConfig.mProviderName.size());
            newDescLen += output->mConfig.mProviderName.size();
            }
          else {
            desc48_set_provider (newDesc, oldProvider, oldProviderLen);
            newDescLen += oldProviderLen;
            }
          if (output->mConfig.mServiceName.size()) {
            desc48_set_service (newDesc, (const uint8_t*)output->mConfig.mServiceName.c_str(),
                                          output->mConfig.mServiceName.size());
            newDescLen += output->mConfig.mServiceName.size();
            }
          else {
            desc48_set_service (newDesc, oldService, oldServiceLen);
            newDescLen += oldServiceLen;
            }

          desc_set_length (newDesc, newDescLen);
          totalDescLen += DESC_HEADER_SIZE + newDescLen;
          newDesc += DESC_HEADER_SIZE + newDescLen;
          }
        else {
          // Copy single descriptor
          int descLen = DESC_HEADER_SIZE + desc_get_length (desc);
          memcpy (newDesc, desc, descLen);
          newDesc += descLen;
          totalDescLen += descLen;
          }
        }
      sdtn_set_desclength (service, totalDescLen);
      }

    service = sdt_get_service (sdtSection, 1);
    if (service)
      sdt_set_length (sdtSection, service - sdtSection - SDT_HEADER_SIZE);
    else
      // This shouldn't happen if the incoming SDT is valid
      sdt_set_length (sdtSection, 0);

    psi_set_crc (output->mSdtSection);
    }
  //}}}

  //{{{
  void updatePAT (uint16_t sid) {

    for (auto output : mOutputs)
      if (output->mConfig.mSid == sid)
        newPAT (output);
    }
  //}}}
  //{{{
  void updatePMT (uint16_t sid) {

    for (auto output : mOutputs)
      if (output->mConfig.mSid == sid)
        newPMT (output);
    }
  //}}}
  //{{{
  void updateSDT (uint16_t sid) {

    for (auto output : mOutputs)
      if (output->mConfig.mSid == sid)
        newSDT (output);
    }
  //}}}
  //{{{
  void updateTsid() {

    uint16_t tsid = psi_table_get_tableidext (mCurrentPatSections);

    for (auto output : mOutputs)
      if (output->mConfig.mTsId == -1) {
        output->mTsId = tsid;
        newNIT (output);
        }
    }
  //}}}
  //{{{
  void markPmtPids (uint8_t* pmt, uint8_t pid_map[], uint8_t marker) {

    uint16_t pcrPid = pmt_get_pcrpid (pmt);
    if (pcrPid != PADDING_PID)
      pid_map[pcrPid] |= marker;

    uint16_t j = 0;
    uint8_t* es;
    while ((es = pmt_get_es (pmt, j))) {
      uint16_t pidNum = pmtn_get_pid (es);
      j++;

      if (pidWouldBeSelected (es))
        pid_map[pidNum] |= marker;

      mTsPids[pidNum].mPes = pidCarriesPES (es);
      }
    }
  //}}}
  //}}}

  //  setup output
  //{{{
  struct addrinfo* parseHost (const char* hostStr, uint16_t defaultPort) {
  // !!! clean this up !!!

    char* ppsz_end = (char*)hostStr;
    char* strCopy = strdup (hostStr);

    char* psz_node;
    char* psz_end;
    int mFamily = AF_INET;
    if (strCopy[0] == '[') {
      //{{{  look for ipv6 address
      mFamily = AF_INET6;
      psz_node = strCopy + 1;
      psz_end = strchr( psz_node, ']');
      if (psz_end == NULL) {
        cLog::log (LOGERROR, "invalid IPv6 address %s", hostStr);
        free (strCopy);
        return NULL;
        }
      *psz_end++ = '\0';
      }
      //}}}
    else {
      psz_node = strCopy;
      psz_end = strpbrk (strCopy, "@:,/");
      }

    char* psz_port = NULL;
    if (psz_end != NULL && psz_end[0] == ':') {
      //{{{  look for port
      *psz_end++ = '\0';
      psz_port = psz_end;
      psz_end = strpbrk (psz_port, "@:,/");
      }
      //}}}

    if (psz_end != NULL) {
      *ppsz_end = '\0';
      if (ppsz_end != NULL)
        ppsz_end = (char*)hostStr + (psz_end - strCopy);
      }
    else if (ppsz_end != NULL)
      ppsz_end = (char*)hostStr + strlen (hostStr);

    char psz_port_buffer[6];
    if (defaultPort != 0 && (psz_port == NULL || !*psz_port)) {
      sprintf (psz_port_buffer, "%u", defaultPort);
      psz_port = psz_port_buffer;
      }

    if (psz_node[0] == '\0') {
      free (strCopy);
      return NULL;
      }

    struct addrinfo hint;
    memset (&hint, 0, sizeof(hint));
    hint.ai_family = mFamily;
    hint.ai_socktype = SOCK_DGRAM;
    hint.ai_protocol = 0;
    hint.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV | AI_ADDRCONFIG;

    struct addrinfo* addr;
    int ret = getaddrinfo (psz_node, psz_port, NULL, &addr);
    if (ret) {
      cLog::log (LOGINFO, "getaddrinfo host:%s port:%s error:%s",
                 psz_node, psz_port ? psz_port : "", gai_strerror (ret));
      free (strCopy);
      return NULL;
      }

    free (strCopy);
    return addr;
    }
  //}}}
  //{{{
  cOutput* findOutput (const cOutputConfig& config) {

    socklen_t sockaddrLen = (config.mFamily == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

    for (auto output : mOutputs) {
      if (config.mFamily != output->mConfig.mFamily ||
          memcmp (&config.mBindAddr, &output->mConfig.mBindAddr, sockaddrLen) ||
          memcmp (&config.mConnectAddr, &output->mConfig.mConnectAddr, sockaddrLen))
        continue;

      if ((config.mFamily == AF_INET6) && config.mIndexV6 != output->mConfig.mIndexV6)
        continue;

      return output;
      }

    return NULL;
    }
  //}}}
  //{{{
  cOutput* createOutput (const cOutputConfig* config) {

    auto output = new cOutput();
    if (output->initialise (config)) {
      mOutputs.push_back (output);
      return output;
      }

    delete output;
    return NULL;
    }
  //}}}
  //{{{
  void changeOutput (cOutput* output, const cOutputConfig* config) {

    bool sidChanged = output->mConfig.mSid != config->mSid;
    bool dvbChanged = output->mConfig.mOutputDvb != config->mOutputDvb;
    bool epgChanged = output->mConfig.mOutputEpg != config->mOutputEpg;
    bool networkChanged = (output->mConfig.mNetworkId != config->mNetworkId) ||
                          (output->mConfig.mNetworkName != config->mNetworkName);
    bool mServiceChanged = output->mConfig.mServiceName != config->mServiceName;
    bool mProviderChanged = output->mConfig.mProviderName != config->mProviderName;

    output->mConfig.mOutputDvb = config->mOutputDvb;
    output->mConfig.mOutputEpg = config->mOutputEpg;
    output->mConfig.mNetworkId = config->mNetworkId;
    output->mConfig.mNewSid = config->mNewSid;
    output->mConfig.mOnid = config->mOnid;

    // change output settings related to names
    output->mConfig.mNetworkName = config->mNetworkName;
    output->mConfig.mServiceName = config->mServiceName;
    output->mConfig.mProviderName = config->mProviderName;

    bool tsidChanged = false;
    if ((config->mTsId != -1) && (output->mConfig.mTsId != config->mTsId)) {
      output->mTsId = config->mTsId;
      output->mConfig.mTsId = config->mTsId;
      tsidChanged = true;
      }
    if ((config->mTsId == -1) && (output->mConfig.mTsId != -1)) {
      output->mConfig.mTsId = config->mTsId;
      if (psi_table_validate (mCurrentPatSections))
        output->mTsId = psi_table_get_tableidext (mCurrentPatSections);
      tsidChanged = true;
      }

    bool pidChanged = false;
    if (!(!sidChanged &&
         (config->mNumPids == output->mConfig.mNumPids) &&
         (!config->mNumPids || !memcmp (output->mConfig.mPids, config->mPids, config->mNumPids * sizeof(uint16_t))))) {
      //{{{  pids Changed
      uint16_t* wantedPids;
      int numWantedPids;
      uint16_t wantedPcrPid;
      uint16_t sidNum = config->mSid;
      uint16_t* pids = config->mPids;
      int numPids = config->mNumPids;
      getPids (&wantedPids, &numWantedPids, &wantedPcrPid, sidNum, pids, numPids);

      uint16_t* currentPids;
      int numCurrentPids;
      uint16_t currentPcrPid;
      uint16_t oldSidNum = output->mConfig.mSid;
      uint16_t* oldPids = output->mConfig.mPids;
      int oldNumPids = output->mConfig.mNumPids;
      getPids (&currentPids, &numCurrentPids, &currentPcrPid, oldSidNum, oldPids, oldNumPids);

      if (sidChanged && oldSidNum) {
        sSid* oldSid = findSid (oldSidNum);
        output->mConfig.mSid = config->mSid;
        if (oldSid)
          if (sidNum != oldSidNum)
            unselectPMT (oldSidNum, oldSid->mPmtPid);
        }

      for (int i = 0; i < numCurrentPids; i++) {
        if (!isIn (wantedPids, numWantedPids, currentPids[i])) {
          stopPid (output, currentPids[i]);
          pidChanged = true;
          }
        }

      for (int i = 0; i < numWantedPids; i++) {
        if (!isIn (currentPids, numCurrentPids, wantedPids[i])) {
          startPid (output, wantedPids[i]);
          pidChanged = true;
          }
        }

      free (wantedPids);
      free (currentPids);
      output->mPcrPid = wantedPcrPid;

      if (sidChanged && sidNum) {
        sSid* sid = findSid (sidNum);
        output->mConfig.mSid = oldSidNum;
        if (sid)
          if (sidNum != oldSidNum)
            selectPMT (sidNum, sid->mPmtPid);
        }

      output->mConfig.mSid = sidNum;
      free (output->mConfig.mPids);

      output->mConfig.mPids = (uint16_t*)malloc (sizeof(uint16_t) * numPids);
      memcpy (output->mConfig.mPids, pids, sizeof(uint16_t) * numPids);
      output->mConfig.mNumPids = numPids;
      }
      //}}}

    if (sidChanged || pidChanged || tsidChanged || dvbChanged || networkChanged || mServiceChanged || mProviderChanged)
      cLog::log (LOGINFO, format ("changeOuput {}{}{}{}{}{}{}",
        dvbChanged ? "dvb " : "",
        sidChanged ? "sid " : "", pidChanged ? "pid " : "", tsidChanged ? "tsid " : "",
        networkChanged ? "network " : "", mServiceChanged ? "service " : "", mProviderChanged ? "provider " : ""));

    if (sidChanged || tsidChanged || dvbChanged)
      newPAT (output);
    if (sidChanged || pidChanged)
      newPMT (output);
    if (sidChanged || tsidChanged ||mServiceChanged || mProviderChanged || epgChanged)
      newSDT (output);
    if (sidChanged || tsidChanged || dvbChanged || networkChanged)
      newNIT (output);

    memcpy (output->mConfig.mSsrc, config->mSsrc, sizeof (config->mSsrc));

    int ret = 0;
    if (output->mConfig.mTtl != config->mTtl) {
      //{{{  new ttl
      if (output->mConfig.mFamily == AF_INET6) {
        struct sockaddr_in6* p_addr = (struct sockaddr_in6 *)&output->mConfig.mConnectAddr;
        if (IN6_IS_ADDR_MULTICAST(&p_addr->sin6_addr))
          ret = setsockopt (output->mSocket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                            (void*)&config->mTtl, sizeof(config->mTtl));
        }
      else {
        struct sockaddr_in* p_addr = (struct sockaddr_in*)&output->mConfig.mConnectAddr;
        if (IN_MULTICAST(ntohl (p_addr->sin_addr.s_addr)))
          ret = setsockopt (output->mSocket, IPPROTO_IP, IP_MULTICAST_TTL,
                            (void*)&config->mTtl, sizeof(config->mTtl));
        }

      output->mConfig.mTtl = config->mTtl;
      }
      //}}}
    if (output->mConfig.mTos != config->mTos) {
      //{{{  new tos
      if (output->mConfig.mFamily == AF_INET)
        ret = setsockopt (output->mSocket, IPPROTO_IP, IP_TOS, (void*)&config->mTos, sizeof(config->mTos));

      output->mConfig.mTos = config->mTos;
      }
      //}}}
    if (ret == -1)
      cLog::log (LOGERROR, "socket change failed %s", strerror(errno));

    if (output->mConfig.mMtu != config->mMtu) {
      //{{{  new mtu
      sRtpPacket* packet = output->mLastPacket;
      output->mConfig.mMtu = config->mMtu;

      output->packetCleanup();
      int blockCount = output->getBlockCount();
      if (packet && (packet->mDepth < blockCount)) {
        packet = (sRtpPacket*)realloc (packet, sizeof(sRtpPacket*) + blockCount * sizeof(cTsBlock*));
        output->mLastPacket = packet;
        }
      }
      //}}}
    }
  //}}}

  // send output
  //{{{
  void outputPut (cOutput* output, cTsBlock* block) {
  // assemble ts block into tcp packets, not sure why there is more than one packet for each output, timestamp?

    block->incRefCount();
    int blockCount = output->getBlockCount();

    if (output->mLastPacket && (output->mLastPacket->mDepth < blockCount)) {
      // add ts block to partial rtp packet
      sRtpPacket* packet = output->mLastPacket;
      if (ts_has_adaptation (block->mTs) && ts_get_adaptation (block->mTs) && tsaf_has_pcr (block->mTs))
        packet->mDts = block->mDts;

      packet->mBlocks[packet->mDepth] = block;
      packet->mDepth++;

      if (packet->mDepth == blockCount) {
        // send packet
        packet = output->mPackets;

        //{{{  form rtp packet iovecs
        // udp rtp header layout
        //  0                   1                   2                   3
        //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |V=2|P|X|  CC   |M|     PT      |       sequence number         |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |                           timestamp                           |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        // |           synchronization source (SSRC) identifier            |
        // +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
        // |            contributing source (CSRC) identifiers             |
        // |                             ....                              |
        // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

        struct iovec iovecs[blockCount + 2];
        uint8_t rtpHeader[kRtpHeaderSize];

        iovecs[0].iov_base = rtpHeader;
        iovecs[0].iov_len = kRtpHeaderSize;

        // set rtpHeader
        rtpHeader[0] = 0x80;
        rtpHeader[1] = 33; // RTP_TYPE_TS & 0x7f;
        rtpHeader[2] = output->mSeqnum >> 8;
        rtpHeader[3] = output->mSeqnum & 0xff;
        output->mSeqnum++;

        // timestamp based only on local time when sent 90 kHz clock = 90000 counts per second
        uint32_t timestamp = mWallclock * 9/100;
        rtpHeader[4] = (timestamp >> 24) & 0xff;
        rtpHeader[5] = (timestamp >> 16) & 0xff;
        rtpHeader[6] = (timestamp >> 8) & 0xff;
        rtpHeader[7] = timestamp & 0xff;

        // set ssrc
        rtpHeader[8] = output->mConfig.mSsrc[0];
        rtpHeader[9] = output->mConfig.mSsrc[1];
        rtpHeader[10] = output->mConfig.mSsrc[2];
        rtpHeader[11] = output->mConfig.mSsrc[3];

        // rtp body
        int numIov = 1;

        int blockNum;
        for (blockNum = 0; blockNum < packet->mDepth; blockNum++) {
          iovecs[numIov].iov_base = packet->mBlocks[blockNum]->mTs;
          iovecs[numIov].iov_len = 188;
          numIov++;
          }

        // rtp padding
        for (; blockNum < blockCount; blockNum++) {
          iovecs[numIov].iov_base = kPadTs;
          iovecs[numIov].iov_len = 188;
          numIov++;
          }
        //}}}

        // send rtp packet iovecs
        if (writev (output->mSocket, iovecs, numIov) < 0)
          cLog::log (LOGERROR, "outputPut writev failed " + output->mConfig.mDisplayName);

        // update the wallclock because writev() can take some time
        mWallclock = mdate();

        // release packets
        for (blockNum = 0; blockNum < packet->mDepth; blockNum++)
          mBlockPool->unRefBlock (packet->mBlocks[blockNum]);

        output->mPackets = packet->mNextPacket;
        output->packetDelete (packet);
        if (output->mPackets == NULL)
          output->mLastPacket = NULL;
        }
      }

    else {
      // start new rtp packet
      sRtpPacket* packet = output->packetNew();
      packet->mDts = block->mDts;
      if (output->mLastPacket)
        output->mLastPacket->mNextPacket = packet;
      else
        output->mPackets = packet;

      // add packet to packetList
      output->mLastPacket = packet;
      packet->mBlocks[packet->mDepth] = block;
      packet->mDepth++;
      }
    }
  //}}}
  //{{{
  void outputPsiSection (cOutput* output, uint8_t* section,
                         uint16_t pidNum, uint8_t* continuity, int64_t dts,
                         cTsBlock** tsBuffer, uint8_t* tsBufferOffset) {

    uint16_t sectionOffset = 0;
    uint16_t sectionLength = psi_get_length(section) + PSI_HEADER_SIZE;

    do {
      bool append = tsBuffer && *tsBuffer;

      cTsBlock* block;
      uint8_t tsOffset;
      if (append) {
        block = *tsBuffer;
        tsOffset = *tsBufferOffset;
        }
      else {
        block = mBlockPool->newBlock();
        block->mDts = dts;
        tsOffset = 0;
        }
      uint8_t* p = block->mTs;

      psi_split_section (p, &tsOffset, section, &sectionOffset);

      if (!append) {
        ts_set_pid (p, pidNum);
        ts_set_cc (p, *continuity);
        (*continuity)++;
        *continuity &= 0xf;
        }

      if (sectionOffset == sectionLength) {
        if (tsOffset < 188 - MIN_SECTION_FRAGMENT && tsBuffer) {
          *tsBuffer = block;
          *tsBufferOffset = tsOffset;
          break;
          }
        else
          psi_split_end (p, &tsOffset);
        }

      block->mDts = dts;
      block->decRefCount();
      outputPut (output, block);

      if (tsBuffer) {
        *tsBuffer = NULL;
        *tsBufferOffset = 0;
        }
      } while (sectionOffset < sectionLength);
    }
  //}}}
  //{{{
  void sendPAT (int64_t dts) {

    for (auto output : mOutputs) {
      if ((output->mPatSection == NULL) && psi_table_validate (mCurrentPatSections)) {
        // SID doesn't exist - build an empty PAT
        uint8_t* patSection = psi_allocate();
        output->mPatSection = patSection;
        output->mPatVersion++;

        pat_init (patSection);
        pat_set_length (patSection, 0);
        pat_set_tsid (patSection, output->mTsId);
        psi_set_version (patSection, output->mPatVersion);
        psi_set_current (patSection);
        psi_set_section (patSection, 0);
        psi_set_lastsection (patSection, 0);
        psi_set_crc (output->mPatSection);
        }

      if (output->mPatSection)
        outputPsiSection (output, output->mPatSection, PAT_PID, &output->mPatContinuity, dts, NULL, NULL);
      }
    }
  //}}}
  //{{{
  void sendPMT (sSid* sid, int64_t dts) {

    int pmtPid = sid->mPmtPid;

    for (auto output: mOutputs)
      if ((output->mConfig.mSid == sid->mSid) && output->mPmtSection)
        outputPsiSection (output, output->mPmtSection, pmtPid, &output->mPmtContinuity, dts, NULL, NULL);
    }
  //}}}
  //{{{
  void sendNIT (int64_t dts) {

    for (auto output: mOutputs)
      if (output->mConfig.mOutputDvb && output->mNitSection)
        outputPsiSection (output, output->mNitSection, NIT_PID, &output->mNitContinuity, dts, NULL, NULL);
    }
  //}}}
  //{{{
  void sendSDT (int64_t dts) {

    for (auto output : mOutputs)
      if (output->mConfig.mOutputDvb && output->mSdtSection)
        outputPsiSection (output, output->mSdtSection, SDT_PID, &output->mSdtContinuity, dts, NULL, NULL);
    }
  //}}}
  //{{{
  void sendTDT (cTsBlock* block) {

    for (auto output : mOutputs)
      if (output->mConfig.mOutputDvb && output->mSdtSection)
        outputPut (output, block);
    }
  //}}}
  //{{{
  bool handleEpg (int tableId) {

    return (tableId == EIT_TABLE_ID_PF_ACTUAL) ||
           ((tableId >= EIT_TABLE_ID_SCHED_ACTUAL_FIRST) && (tableId <= EIT_TABLE_ID_SCHED_ACTUAL_LAST));
    }
  //}}}
  //{{{
  void sendEIT (sSid* sid, int64_t dts, uint8_t* eit) {

    bool epg = handleEpg (psi_get_tableid (eit));
    uint16_t onid = eit_get_onid (eit);

    for (auto output: mOutputs) {
      if (output->mConfig.mOutputDvb &&
          (!epg || output->mConfig.mOutputEpg) && (output->mConfig.mSid == sid->mSid)) {
        eit_set_tsid (eit, output->mTsId);

        if (output->mConfig.mNewSid)
          eit_set_sid (eit, output->mConfig.mNewSid);
        else
          eit_set_sid (eit, output->mConfig.mSid);

        if (output->mConfig.mOnid)
          eit_set_onid (eit, output->mConfig.mOnid);

        psi_set_crc (eit);

        outputPsiSection (output, eit, EIT_PID, &output->mEitContinuity,
                          dts, &output->mEitTsBuffer, &output->mEit_ts_buffer_offset);

        if (output->mConfig.mOnid)
          eit_set_onid (eit, onid);
        }
      }
    }
  //}}}
  //{{{
  void flushEIT (cOutput* output, int64_t dts) {

    cTsBlock* block = output->mEitTsBuffer;
    psi_split_end (block->mTs, &output->mEit_ts_buffer_offset);

    block->mDts = dts;
    block->decRefCount();
    outputPut (output, block);

    output->mEitTsBuffer = NULL;
    output->mEit_ts_buffer_offset = 0;
    }
  //}}}

  // demux
  //{{{
  void deleteProgram (uint16_t sidNum, uint16_t pidNum) {

    unselectPMT (sidNum, pidNum);

    sSid* sid = findSid (sidNum);
    if (sid == NULL)
      return;

    uint8_t* pmt = sid->mCurrentPmt;
    if (pmt) {
      uint16_t pcrPid = pmt_get_pcrpid (pmt);

      if (pcrPid != PADDING_PID && pcrPid != sid->mPmtPid)
        unselectPid (sidNum, pcrPid);

      uint8_t j = 0;
      uint8_t* es;
      while ((es = pmt_get_es (pmt, j))) {
        uint16_t esPid = pmtn_get_pid (es);
        j++;
        if (pidWouldBeSelected (es))
          unselectPid (sidNum, esPid);
        }

      free (pmt);
      sid->mCurrentPmt = NULL;
      }

    sid->mSid = 0;
    sid->mPmtPid = 0;
    for (uint8_t table = 0; table < MAX_EIT_TABLES; table++) {
      psi_table_free (sid->mEitTables[table].data);
      psi_table_init (sid->mEitTables[table].data);
      }
    }
  //}}}
  //{{{
  void handlePAT (int64_t dts) {

    bool change = false;
    PSI_TABLE_DECLARE(oldPatSections);
    uint8_t lastSection = psi_table_get_lastsection (mNextPatSections);

    if (psi_table_validate (mCurrentPatSections) &&
        psi_table_compare (mCurrentPatSections, mNextPatSections)) {
      // identical PAT
      psi_table_free (mNextPatSections);
      psi_table_init (mNextPatSections);
      sendPAT (dts);
      return;
      }

    if (!pat_table_validate (mNextPatSections)) {
      cLog::log (LOGINFO, "invalid PAT received");
      psi_table_free (mNextPatSections);
      psi_table_init (mNextPatSections);
      sendPAT (dts);
      return;
      }

    // Switch tables
    psi_table_copy (oldPatSections, mCurrentPatSections);
    psi_table_copy (mCurrentPatSections, mNextPatSections);
    psi_table_init (mNextPatSections);

    if (!psi_table_validate (oldPatSections) ||
        (psi_table_get_tableidext (mCurrentPatSections) != psi_table_get_tableidext (oldPatSections))) {
      // trigger universal reset of everything
      change = true;
      updateTsid();
      }

    for (int i = 0; i <= lastSection; i++) {
      int j = 0;
      uint8_t* section = psi_table_get_section (mCurrentPatSections, i);
      const uint8_t* program;
      while ((program = pat_get_program (section, j))) {
        const uint8_t* oldProgram = NULL;
        uint16_t sid = patn_get_program (program);
        uint16_t pid = patn_get_pid (program);
        j++;

        if (sid == 0) {
          if (pid != NIT_PID)
            cLog::log (LOGINFO, "nit is carried on PID %hu which isn't DVB compliant", pid);
          continue; // NIT
          }

          if (!psi_table_validate (oldPatSections) ||
              ((oldProgram = pat_table_find_program (oldPatSections, sid)) == NULL) ||
              patn_get_pid (oldProgram) != pid ||
              change) {
            sSid* p_sid;

          if (oldProgram)
            deleteProgram (sid, patn_get_pid (oldProgram));

          selectPMT (sid, pid);

          p_sid = findSid (0);
          if (p_sid == NULL) {
            // not found, create and add new sid
            p_sid = (sSid*)malloc (sizeof(sSid));
            p_sid->mCurrentPmt = NULL;
            for (int table = 0; table < MAX_EIT_TABLES; table++)
              psi_table_init (p_sid->mEitTables[table].data);

            mNumSids++;
            mSids = (sSid**)realloc (mSids, sizeof(sSid*) * mNumSids);
            mSids[mNumSids - 1] = p_sid;
            }

          p_sid->mSid = sid;
          p_sid->mPmtPid = pid;
          updatePAT (sid);
          }
        }
      }

    if (psi_table_validate (oldPatSections)) {
      lastSection = psi_table_get_lastsection (oldPatSections);
      for (int i = 0; i <= lastSection; i++) {
        uint8_t* section = psi_table_get_section (oldPatSections, i);
        const uint8_t* program;
        int j = 0;
        while ((program = pat_get_program (section, j))) {
          uint16_t sid = patn_get_program (program);
          uint16_t pid = patn_get_pid (program);
          j++;

          if (sid == 0)
            continue; // NIT met

          if (pat_table_find_program (mCurrentPatSections, sid) == NULL) {
            deleteProgram (sid, pid);
            updatePAT (sid);
            }
          }
        }

      psi_table_free (oldPatSections);
      }

    sendPAT (dts);
    }
  //}}}
  //{{{
  void handlePATSection (uint16_t pid, uint8_t* section, int64_t dts) {

    if ((pid != PAT_PID) || !pat_validate (section)) {
      cLog::log (LOGERROR, "invalid pat section pid:%hu", pid);
      free (section);
      return;
      }

    if (!psi_table_section (mNextPatSections, section))
      return;

    handlePAT (dts);
    }
  //}}}
  //{{{
  void handlePMT (uint16_t pid, uint8_t* pmt, int64_t dts) {

    uint16_t sidNum = pmt_get_program (pmt);
    sSid* sid = findSid (sidNum);
    if (sid == NULL) {
      // unwanted SID (happens when the same PMT PID is used for several programs).
      free (pmt);
      return;
      }

    if (pid != sid->mPmtPid) {
      cLog::log (LOGERROR, "invalid pmt section pid:%hu", pid);
      free (pmt);
      return;
      }

    if (sid->mCurrentPmt && psi_compare (sid->mCurrentPmt, pmt)) {
      // Identical PMT
      free (pmt);
      sendPMT (sid, dts);
      return;
      }

    if  (!pmt_validate (pmt)) {
      cLog::log (LOGERROR, "invalid pmt section pid:%hu", pid);
      free (pmt);
      sendPMT (sid, dts);
      return;
      }

    uint8_t pid_map[MAX_PIDS];
    memset (pid_map, 0, sizeof(pid_map));

    if (sid->mCurrentPmt) {
      markPmtPids (sid->mCurrentPmt, pid_map, 0x02);
      free (sid->mCurrentPmt);
      }

    markPmtPids (pmt, pid_map, 0x01);

    uint16_t pcrPid = pmt_get_pcrpid (pmt);
    for (auto output : mOutputs)
      if (output->mConfig.mSid == sidNum)
        output->mPcrPid = 0;

    // start to stream PIDs
    for (int pid = 0; pid < MAX_PIDS; pid++) {
      // pid does not exist in the old PMT and in the new PMT. Ignore this pid.
      if (!pid_map[pid])
        continue;

      switch (pid_map[pid] & 0x03) {
        case 0x03: // The pid exists in the old PMT and in the new PMT. The pid was already selected in case 0x01.
          continue;

        case 0x02: // The pid does not exist in the new PMT but exists in the old PMT. Unselect it
          unselectPid (sidNum, pid);
          break;

        case 0x01: // The pid exists in new PMT. Select it
          selectPid (sidNum, pid, pid == pcrPid);
          break;
        }
      }

    sid->mCurrentPmt = pmt;
    updatePMT (sidNum);

    sendPMT (sid, dts);
    }
  //}}}
  //{{{
  void handleNIT (int64_t dts) {

    if (psi_table_validate (mCurrentNitSections) &&
        psi_table_compare (mCurrentNitSections, mNexttNitSections)) {
      // Identical NIT. Shortcut
      psi_table_free (mNexttNitSections);
      psi_table_init (mNexttNitSections);
      return;
      }

    if (!nit_table_validate (mNexttNitSections)) {
      cLog::log (LOGERROR, "invalid NIT received");
      psi_table_free (mNexttNitSections);
      psi_table_init (mNexttNitSections);
      return;
      }

    // Switch tables
    psi_table_free (mCurrentNitSections);
    psi_table_copy (mCurrentNitSections, mNexttNitSections);
    psi_table_init (mNexttNitSections);
    }
  //}}}
  //{{{
  void handleNITSection (uint16_t pid, uint8_t* section, int64_t dts) {

    if (pid != NIT_PID || !nit_validate (section)) {
      cLog::log (LOGERROR, "invalid nit section pid:%hu", pid);
      free (section);
      return;
      }

    if (psi_table_section (mNexttNitSections, section))
      handleNIT (dts);

    // This case is different because DVB specifies a minimum bitrate for PID 0x10
    // even if we don't have any thing to send (for cheap transport over network boundaries)
    sendNIT (dts);
    }
  //}}}
  //{{{
  void handleSDT (int64_t dts) {

    PSI_TABLE_DECLARE(oldSdtSections);
    uint8_t lastSection = psi_table_get_lastsection (mNextSdtSections);

    if (psi_table_validate (mCurrentSdtSections) &&
        psi_table_compare (mCurrentSdtSections, mNextSdtSections)) {
      // identical SDT. Shortcut
      psi_table_free (mNextSdtSections);
      psi_table_init (mNextSdtSections);
      sendSDT (dts);
      return;
      }

    if (!sdt_table_validate (mNextSdtSections)) {
      cLog::log (LOGINFO, "invalid sdt received");
      psi_table_free (mNextSdtSections);
      psi_table_init (mNextSdtSections);
      sendSDT (dts);
      return;
      }

    // switch tables
    psi_table_copy (oldSdtSections, mCurrentSdtSections);
    psi_table_copy (mCurrentSdtSections, mNextSdtSections);
    psi_table_init (mNextSdtSections);

    int j;
    for (int i = 0; i <= lastSection; i++) {
      uint8_t* section = psi_table_get_section (mCurrentSdtSections, i);
      j = 0;
      uint8_t* service;
      while ((service = sdt_get_service (section, j))) {
        uint16_t sid = sdtn_get_sid (service);
        j++;
        updateSDT (sid);
        }
      }

    if (psi_table_validate (oldSdtSections)) {
      lastSection = psi_table_get_lastsection (oldSdtSections);
      for (int i = 0; i <= lastSection; i++) {
        uint8_t* section = psi_table_get_section (oldSdtSections, i);
        int j = 0;
        const uint8_t* service;
        while ((service = sdt_get_service (section, j))) {
          uint16_t sid = sdtn_get_sid (service);
          j++;
          if (sdt_table_find_service (mCurrentSdtSections, sid) == NULL)
            updateSDT (sid);
          }
        }

      psi_table_free (oldSdtSections);
      }

    sendSDT (dts);
    }
  //}}}
  //{{{
  void handleSDTSection (uint16_t pid, uint8_t* section, int64_t dts) {

    if ((pid != SDT_PID) || !sdt_validate (section)) {
      cLog::log (LOGERROR, "invalid SDT section pid:%hu", pid);
      free (section);
      return;
      }

    if (!psi_table_section (mNextSdtSections, section))
      return;

    handleSDT (dts);
    }
  //}}}
  //{{{
  void handleEIT (uint16_t pid, uint8_t* eit, int64_t dts) {

    uint8_t tableId = psi_get_tableid (eit);

    uint16_t sidNum = eit_get_sid (eit);
    sSid* sid = findSid (sidNum);
    if (!sid) {
      // Not a selected program
      free (eit);
      return;
      }

    if ((pid != EIT_PID) || !eit_validate (eit)) {
      cLog::log (LOGINFO, "invalid eit section pid:%hu", pid);
      free (eit);
      return;
      }

    bool epg = handleEpg (tableId);
    if (!epg) {
      sendEIT (sid, dts, eit);
      if (!epg)
        free (eit);
      return;
      }

    // We do not use psi_table_* primitives as the spec allows for holes in
    // section numbering, and there is no sure way to know whether you have gathered all sections
    uint8_t iSection = psi_get_section (eit);
    uint8_t eitTableId = tableId - EIT_TABLE_ID_PF_ACTUAL;
    if (eitTableId >= MAX_EIT_TABLES) {
      sendEIT (sid, dts, eit);
      if (!epg)
        free (eit);
      return;
      }

    if (sid->mEitTables[eitTableId].data[iSection] &&
      psi_compare (sid->mEitTables[eitTableId].data[iSection], eit)) {
      // Identical section Shortcut
      free (sid->mEitTables[eitTableId].data[iSection]);
      sid->mEitTables[eitTableId].data[iSection] = eit;
      sendEIT (sid, dts, eit);
      if (!epg)
        free (eit);
      return;
      }

    free (sid->mEitTables[eitTableId].data[iSection]);
    sid->mEitTables[eitTableId].data[iSection] = eit;

    sendEIT (sid, dts, eit);
    if (!epg)
      free (eit);
    }
  //}}}
  //{{{
  void handleSection (uint16_t pid, uint8_t* section, int64_t dts) {

    if (!psi_validate (section)) {
      cLog::log (LOGERROR, "invalid section pid:%hu", pid);
      free (section);
      return;
      }

    if (!psi_get_current (section)) {
      // ignore sections which are not in use yet
      free (section);
      return;
      }

    uint8_t tableId = psi_get_tableid (section);
    switch (tableId) {
      case PAT_TABLE_ID:
        handlePATSection (pid, section, dts);
        break;

      case PMT_TABLE_ID:
        handlePMT (pid, section, dts);
        break;

      case NIT_TABLE_ID_ACTUAL:
        handleNITSection (pid, section, dts);
        break;

      case SDT_TABLE_ID_ACTUAL:
        handleSDTSection (pid, section, dts);
        break;

      default:
        if (handleEpg (tableId)) {
          handleEIT (pid, section, dts);
          break;
          }
        free (section);
        break;
      }
    }
  //}}}
  //{{{
  void handlePsiPacket (uint8_t* ts, int64_t dts) {

    uint16_t pid = ts_get_pid (ts);
    sTsPid* tsPid = &mTsPids[pid];

    uint8_t continuity = ts_get_cc (ts);
    if (ts_check_duplicate (continuity, tsPid->mLastContinuity) || !ts_has_payload (ts))
      return;

    if ((tsPid->mLastContinuity != -1) &&
        ts_check_discontinuity (continuity, tsPid->mLastContinuity))
      psi_assemble_reset (&tsPid->mPsiBuffer, &tsPid->mPsiBufferUsed);

    const uint8_t* payload = ts_section (ts);
    uint8_t length = ts + 188 - payload;
    if (!psi_assemble_empty (&tsPid->mPsiBuffer, &tsPid->mPsiBufferUsed)) {
      uint8_t* section = psi_assemble_payload (&tsPid->mPsiBuffer, &tsPid->mPsiBufferUsed, &payload, &length);
      if (section)
        handleSection (pid, section, dts);
      }

    payload = ts_next_section (ts);
    length = ts + 188 - payload;
    while (length) {
      uint8_t* section = psi_assemble_payload (&tsPid->mPsiBuffer, &tsPid->mPsiBufferUsed, &payload, &length);
      if (section)
        handleSection (pid, section, dts);
      }
    }
  //}}}
  //{{{
  void demux (cTsBlock* block) {
  // demux single ts block

    uint16_t pidNum = ts_get_pid (block->mTs);
    sTsPid* tsPid = &mTsPids[pidNum];
    uint8_t continuity = ts_get_cc (block->mTs);

    mNumPackets++;
    if (!ts_validate (block->mTs)) {
      //{{{  error return
      cLog::log (LOGERROR, "lost TS sync");
      mBlockPool->freeBlock (block);
      mNumInvalids++;
      return;
      }
      //}}}

    if ((pidNum != PADDING_PID) &&
        (tsPid->mLastContinuity != -1) &&
        !ts_check_duplicate (continuity, tsPid->mLastContinuity) &&
        ts_check_discontinuity (continuity, tsPid->mLastContinuity)) {
      // inc error counts
      mNumDiscontinuities++;

      // get and log info
      uint16_t sid = 0;
      const char* pidDesc = getPidDesc (pidNum, &sid);
      cLog::log (LOGERROR, format ("continuity sid:{} pid:{} {}:{} {}",
                            sid, pidNum, continuity, (tsPid->mLastContinuity + 1) & 0x0f, pidDesc));
      }

    if (ts_get_transporterror (block->mTs)) {
      // get and log info
      uint16_t sid = 0;
      const char* desc = getPidDesc (pidNum, &sid);
      cLog::log (LOGERROR, format ("transportErorIndicator pid:{} {} sid:{}", pidNum, desc, sid));

      // inc error counts
      mNumErrors++;
      mTunerErrors++;
      }

    if (mTunerErrors > MAX_ERRORS) {
      mTunerErrors = 0;
      mDvb->reset();
      }

    if (!ts_get_transporterror (block->mTs)) {
      // parse psi
      if (pidNum == TDT_PID || pidNum == RST_PID)
        sendTDT (block);
      else if (tsPid->mPsiRefCount)
        handlePsiPacket (block->mTs, block->mDts);
      }
    tsPid->mLastContinuity = continuity;

    // output
    for (int i = 0; i < tsPid->mNumOutputs; i++) {
      cOutput* output = tsPid->mOutputs[i];
      if ((output->mPcrPid != pidNum) ||
          (ts_has_adaptation (block->mTs) && ts_get_adaptation (block->mTs) && tsaf_has_pcr (block->mTs)))
        outputPut (output, block);

      if (output->mEitTsBuffer &&
          (block->mDts > output->mEitTsBuffer->mDts + MAX_EIT_RETENTION))
        flushEIT (output, block->mDts);
      }

    mBlockPool->unRefBlock (block);
    }
  //}}}

  // close output
  //{{{
  void outputClose (cOutput* output) {

    sRtpPacket* packet = output->mPackets;
    while (packet) {
      for (int i = 0; i < packet->mDepth; i++)
        mBlockPool->unRefBlock (packet->mBlocks[i]);
      output->mPackets = packet->mNextPacket;
      output->packetDelete (packet);
      packet = output->mPackets;
      }
    output->packetCleanup();

    output->mPackets = output->mLastPacket = NULL;
    free (output->mPatSection);
    free (output->mPmtSection);
    free (output->mNitSection);
    free (output->mSdtSection);
    free (output->mEitTsBuffer);

    close (output->mSocket);

    free (output->mConfig.mPids);
    }
  //}}}
  }

// cDvbRtp
//{{{
cDvbRtp::cDvbRtp (cDvb* dvb, cTsBlockPool* blockPool) {

  mDvb = dvb;
  mBlockPool= blockPool;

  memset (mTsPids, 0, sizeof(mTsPids));
  for (int i = 0; i < MAX_PIDS; i++) {
    mTsPids[i].mLastContinuity = -1;
    mTsPids[i].mDemuxFd = -1;
    psi_assemble_init (&mTsPids[i].mPsiBuffer, &mTsPids[i].mPsiBufferUsed);
    }

  if (mBudgetMode)
    mBudgetDemuxFd = mDvb->setFilter (8192);

  psi_table_init (mCurrentPatSections);
  psi_table_init (mNextPatSections);

  setPid (PAT_PID);
  mTsPids[PAT_PID].mPsiRefCount++;

  setPid (NIT_PID);
  mTsPids[NIT_PID].mPsiRefCount++;

  psi_table_init (mCurrentSdtSections);
  psi_table_init (mNextSdtSections);

  setPid (SDT_PID);
  mTsPids[SDT_PID].mPsiRefCount++;

  setPid (EIT_PID);
  mTsPids[EIT_PID].mPsiRefCount++;

  setPid (RST_PID);
  setPid (TDT_PID);
  }
//}}}
//{{{
cDvbRtp::~cDvbRtp() {

  // close tables
  psi_table_free (mCurrentPatSections);
  psi_table_free (mNextPatSections);
  psi_table_free (mCurrentCatSections);
  psi_table_free (mNextCatSections);
  psi_table_free (mCurrentNitSections);
  psi_table_free (mNexttNitSections);
  psi_table_free (mCurrentSdtSections);
  psi_table_free (mNextSdtSections);

  // close pids
  for (int pid = 0; pid < MAX_PIDS; pid++) {
    free (mTsPids[pid].mPsiBuffer);
    free (mTsPids[pid].mOutputs);
    }

  // close sids
  for (int i = 0; i < mNumSids; i++) {
    sSid* sid = mSids[i];
    for (int table = 0; table < MAX_EIT_TABLES; table++)
      psi_table_free (sid->mEitTables[table].data);
    free (sid->mCurrentPmt);
    free (sid);
    }
  free (mSids);

  // close outputs
  for (auto output : mOutputs) {
    cLog::log (LOGINFO, "remove "+ output->mConfig.mDisplayName);
    outputClose (output);
    delete output;
    }

  mOutputs.clear();
  }
//}}}

uint64_t cDvbRtp::getNumPackets() { return mNumPackets; }
uint64_t cDvbRtp::getNumErrors() { return mNumErrors; }
uint64_t cDvbRtp::getNumInvalids() { return mNumInvalids; }
uint64_t cDvbRtp::getNumDiscontinuities() { return mNumDiscontinuities; }

//{{{
bool cDvbRtp::setOutput (const string& outputString, int sid) {

  struct addrinfo* addr = parseHost (outputString.c_str(), DEFAULT_PORT);
  if (!addr)
    return false;

  cOutputConfig outputConfig;
  outputConfig.initialise ("", "");
  outputConfig.mDisplayName = outputString;
  outputConfig.mSid = sid;
  outputConfig.mOutputDvb = true;
  outputConfig.mOutputEpg = true;

  memcpy (&outputConfig.mConnectAddr, addr->ai_addr, addr->ai_addrlen);
  outputConfig.mFamily = outputConfig.mConnectAddr.ss_family;
  freeaddrinfo (addr);

  int mtu = outputConfig.mFamily == AF_INET6 ? DEFAULT_IPV6_MTU : DEFAULT_IPV4_MTU;
  if (outputConfig.mMtu) {
    cLog::log (LOGERROR, "invalid MTU %d, setting %d", outputConfig.mMtu, mtu);
    outputConfig.mMtu = mtu;
    }
  else if (outputConfig.mMtu < 188 + kRtpHeaderSize)
    outputConfig.mMtu = mtu;

  // find output matching this address
  cOutput* output = findOutput (outputConfig);
  if (!output) // not found, create new
    output = createOutput (&outputConfig);

  if (output) {
    output->mConfig.mDisplayName = outputConfig.mDisplayName;
    changeOutput (output, &outputConfig);
    free (outputConfig.mPids);
    return true;
    }

  free (outputConfig.mPids);
  return false;
  }
//}}}
//{{{
void cDvbRtp::processBlockList (cTsBlock* blockList) {
// process block list

  mWallclock = mdate();

  // set blockList DTS
  int numTs = 0;
  cTsBlock* block = blockList;
  while (block) {
    numTs++;
    block = block->mNextBlock;
    }

  // assume CBR, at least between two consecutive read(), especially true in budget mode
  int64_t duration = (mLastDts == -1)  ? 0 : mWallclock - mLastDts;
  block = blockList;
  int i = numTs - 1;
  while (block) {
    block->mDts = mWallclock - duration * i / numTs;
    i--;
    block = block->mNextBlock;
    }
  mLastDts = mWallclock;

  // demux block list
  block = blockList;
  while (block) {
    cTsBlock* nextBlock = block->mNextBlock;
    block->mNextBlock = NULL;
    demux (block);
    block = nextBlock;
    }
  }
//}}}
