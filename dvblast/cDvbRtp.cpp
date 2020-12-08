// dvbRtp.cpp - dvb ts to rtp streams - derived from videoLan dvblast
//{{{  includes
#include "cBlockPool.h"
#include "cDvbRtp.h"
#include "cDvb.h"

#include <sys/uio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>

#include "../../shared/bitstream/dvb/si.h"
#include "../../shared/bitstream/mpeg/ts.h"
#include "../../shared/bitstream/mpeg/pes.h"
#include "../../shared/bitstream/mpeg/psi.h"
#include "../../shared/bitstream/ietf/rtp.h"

// utils
#include "../../shared/fmt/core.h"
#include "../../shared/utils/cLog.h"

using namespace std;
using namespace fmt;
//}}}
//{{{  defines
constexpr int DEFAULT_IPV4_MTU = 1500;
constexpr int DEFAULT_IPV6_MTU = 1280;
constexpr int DEFAULT_PORT = 3001;

constexpr int  MAX_ERRORS = 1000;

constexpr int MAX_POLL_TIMEOUT = 100000;  // 100 ms
constexpr int MIN_POLL_TIMEOUT = 100;     // 100 us

constexpr int MAX_EIT_RETENTION = 500000; // 500 ms

constexpr int MIN_SECTION_FRAGMENT = PSI_HEADER_SIZE_SYNTAX1;

// EIT is carried in several separate tables, we need to track each table
// separately, otherwise one table overwrites sections of another table
constexpr int MAX_EIT_TABLES = EIT_TABLE_ID_SCHED_ACTUAL_LAST - EIT_TABLE_ID_PF_ACTUAL;

constexpr int PADDING_PID = 8191;
constexpr int MAX_PIDS = 8192;
constexpr int UNUSED_PID = MAX_PIDS + 1;

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
  struct sPacket {
    sPacket* mNextPacket;
    int64_t mDts;
    int mDepth;
    cBlock* mBlocks[];
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

    int getBlockCount() { return (mConfig.mMtu - RTP_HEADER_SIZE) / 188; }

    //{{{
    sPacket* packetNew() {

      sPacket* packet;

      if (mPacketCount) {
        packet = mPacketLifo;
        mPacketLifo = packet->mNextPacket;
        mPacketCount--;
        }
      else
        packet = (sPacket*)malloc (sizeof(sPacket) + getBlockCount() * sizeof(cBlock*));

      packet->mDepth = 0;
      packet->mNextPacket = NULL;

      return packet;
      }
    //}}}
    //{{{
    void packetDelete (sPacket* packet) {

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
        sPacket* packet = mPacketLifo;
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

    sPacket* mLastPacket;
    sPacket* mPacketLifo;
    sPacket* mPackets;

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
    cBlock* mEitTsBuffer;

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
  cBlockPool* mBlockPool;

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

  //{{{  demux
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

        // Look for ECMs
        j = 0;
        uint8_t* desc;
        while ((desc = descs_get_desc (pmt_get_descs (mCurrentPmt), j++))) {
          if (desc_get_tag (desc) != 0x09 || !desc09_validate (desc))
            continue;
          if (desc09_get_pid (desc) == pidNum) {
            if (sidNum)
              *sidNum = sid->mSid;
            return "ECM";
            }
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

    sSid* p_sid = findSid (output->mConfig.mSid);
    if (p_sid == NULL)
      return;

    if (p_sid->mCurrentPmt == NULL)
      return;

    uint8_t* currentPmt = p_sid->mCurrentPmt;
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
    output->mNitSection = NULL;
    output->mNitVersion++;

    uint8_t* p = output->mNitSection = psi_allocate();
    nit_init (p, true);
    nit_set_length (p, PSI_MAX_SIZE);
    nit_set_nid (p, output->mConfig.mNetworkId);
    psi_set_version (p, output->mNitVersion);
    psi_set_current (p);
    psi_set_section (p, 0);
    psi_set_lastsection (p, 0);

    if (output->mConfig.mNetworkName.size()) {
      nit_set_desclength (p, DESCS_MAX_SIZE);
      uint8_t* descs = nit_get_descs (p);
      uint8_t* desc = descs_get_desc (descs, 0);
      desc40_init (desc);
      desc40_set_networkname (desc, (const uint8_t*)output->mConfig.mNetworkName.c_str(),
                                     output->mConfig.mNetworkName.size());
      desc = descs_get_desc (descs, 1);
      descs_set_length (descs, desc - descs - DESCS_HEADER_SIZE);
      }
    else
      nit_set_desclength (p, 0);

    uint8_t* header2 = nit_get_header2 (p);
    nith_init (header2);
    nith_set_tslength (header2, NIT_TS_SIZE);

    uint8_t* ts = nit_get_ts (p, 0);
    nitn_init (ts);
    nitn_set_tsid (ts, output->mTsId);
    if (output->mConfig.mOnid)
      nitn_set_onid (ts, output->mConfig.mOnid);
    else
      nitn_set_onid (ts, output->mConfig.mNetworkId);
    nitn_set_desclength (ts, 0);

    ts = nit_get_ts (p, 1);
    if (ts == NULL)
      // This shouldn't happen
      nit_set_length (p, 0);
    else
        nit_set_length (p, ts - p - NIT_HEADER_SIZE);

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

    uint8_t* p = output->mSdtSection = psi_allocate();
    sdt_init (p, true);
    sdt_set_length (p, PSI_MAX_SIZE);
    sdt_set_tsid (p, output->mTsId);
    psi_set_version (p, output->mSdtVersion);
    psi_set_current (p);
    psi_set_section (p, 0);
    psi_set_lastsection (p, 0);
    if (output->mConfig.mOnid)
      sdt_set_onid (p, output->mConfig.mOnid);
    else
      sdt_set_onid (p, sdt_get_onid (psi_table_get_section (mCurrentSdtSections, 0)));

    uint8_t* service = sdt_get_service (p, 0);
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

    service = sdt_get_service (p, 1);
    if (service)
      sdt_set_length (p, service - p - SDT_HEADER_SIZE);
    else
      // This shouldn't happen if the incoming SDT is valid
      sdt_set_length (p, 0);

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
  //{{{  setup output
  //{{{
  struct addrinfo* parseHost (const char* hostStr, uint16_t defaultPort) {
  // should clean this up using string

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
          memcmp (&config.mConnectAddr, &output->mConfig.mConnectAddr, sockaddrLen) ||
          memcmp (&config.mBindAddr, &output->mConfig.mBindAddr, sockaddrLen))
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
    return  NULL;
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

    if (sidChanged) {
       //{{{  new pat,pmt,sdt,nit
       newSDT (output);
       newNIT (output);
       newPAT (output);
       newPMT (output);
       }
       //}}}
    else {
      if (tsidChanged) {
        //{{{  new pat,sdt,nit
        newSDT (output);
        newNIT (output);
        newPAT (output);
        }
        //}}}
      else if (dvbChanged) {
        //{{{  new pat,nit
        newNIT (output);
        newPAT (output);
        }
        //}}}
      else if (networkChanged)
        newNIT (output);
      if (!tsidChanged && (mServiceChanged || mProviderChanged || epgChanged))
        newSDT (output);
      if (pidChanged)
        newPMT (output);
      }

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
      cLog::log (LOGERROR, "couldn't change socket %s", strerror(errno));

    if (output->mConfig.mMtu != config->mMtu) {
      //{{{  new mtu
      sPacket* packet = output->mLastPacket;
      output->mConfig.mMtu = config->mMtu;

      output->packetCleanup();
      int blockCount = output->getBlockCount();
      if (packet && (packet->mDepth < blockCount)) {
        packet = (sPacket*)realloc (packet, sizeof(sPacket*) + blockCount * sizeof(cBlock*));
        output->mLastPacket = packet;
        }
      }
      //}}}
    }
  //}}}
  //}}}
  //{{{  send output
  //{{{
  void outputPut (cOutput* output, cBlock* block) {
  // assemble ts block into tcp packets, not sure why there is more than one packet for each output, timestamp?

    block->incRefCount();
    int blockCount = output->getBlockCount();

    if (output->mLastPacket && (output->mLastPacket->mDepth < blockCount)) {
      // add ts block to partial rtp packet
      sPacket* packet = output->mLastPacket;
      if (ts_has_adaptation (block->mTs) && ts_get_adaptation (block->mTs) && tsaf_has_pcr (block->mTs))
        packet->mDts = block->mDts;

      packet->mBlocks[packet->mDepth] = block;
      packet->mDepth++;

      if (packet->mDepth == blockCount) {
        // send packet
        packet = output->mPackets;
        //{{{  form rtp packet iovecs
        // New timestamp based only on local time when sent 90 kHz clock = 90000 counts per second */
        struct iovec iovecs[blockCount + 2];
        uint8_t rtpHeader[RTP_HEADER_SIZE];

        iovecs[0].iov_base = rtpHeader;
        iovecs[0].iov_len = RTP_HEADER_SIZE;

        rtp_set_hdr (rtpHeader);
        rtp_set_type (rtpHeader, RTP_TYPE_TS);
        rtp_set_seqnum (rtpHeader, output->mSeqnum++);
        rtp_set_timestamp (rtpHeader, mWallclock * 9/100);
        rtp_set_ssrc (rtpHeader, output->mConfig.mSsrc);

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
      sPacket* packet = output->packetNew();
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
                         cBlock** tsBuffer, uint8_t* tsBufferOffset) {

    uint16_t sectionOffset = 0;
    uint16_t sectionLength = psi_get_length(section) + PSI_HEADER_SIZE;

    do {
      bool append = tsBuffer && *tsBuffer;

      cBlock* block;
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
        output->mPatVersion++;

        uint8_t* p = output->mPatSection = psi_allocate();
        pat_init (p);
        pat_set_length (p, 0);
        pat_set_tsid (p, output->mTsId);
        psi_set_version (p, output->mPatVersion);
        psi_set_current (p);
        psi_set_section (p, 0);
        psi_set_lastsection (p, 0);
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
  void sendTDT (cBlock* block) {

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

    cBlock* block = output->mEitTsBuffer;
    psi_split_end (block->mTs, &output->mEit_ts_buffer_offset);

    block->mDts = dts;
    block->decRefCount();
    outputPut (output, block);

    output->mEitTsBuffer = NULL;
    output->mEit_ts_buffer_offset = 0;
    }
  //}}}
  //}}}

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
      cLog::log (LOGINFO, "invalid pat section received pid:%hu", pid);
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
      cLog::log (LOGINFO, "invalid pmt section pid:%hu", pid);
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
      cLog::log (LOGINFO, "invalid pmt section pid:%hu", pid);
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
      cLog::log (LOGINFO, "invalid NIT received");
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
      cLog::log (LOGINFO, "invalid nit section received on pid:%hu", pid);
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
      cLog::log (LOGINFO, "invalid sdt section received on pid:%hu", pid);
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
      cLog::log (LOGINFO, "invalid section pid:%hu", pid);
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
  void demux (cBlock* block) {
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

    sPacket* packet = output->mPackets;
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
cDvbRtp::cDvbRtp (cDvb* dvb, cBlockPool* blockPool) {

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
  else if (outputConfig.mMtu < 188 + RTP_HEADER_SIZE)
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
void cDvbRtp::processBlockList (cBlock* blockList) {
// process block list

  mWallclock = mdate();

  // set blockList DTS
  int numTs = 0;
  cBlock* block = blockList;
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
    cBlock* nextBlock = block->mNextBlock;
    block->mNextBlock = NULL;
    demux (block);
    block = nextBlock;
    }
  }
//}}}
