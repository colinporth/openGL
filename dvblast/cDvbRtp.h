// cDvbRtp.h - dodgy singleton
#pragma once
//{{{  includes
#include <cstdlib>
#include <cstdint>
#include <string>

struct cBlock;
class cDvb;
//}}}

class cDvbRtp {
public:
  cDvbRtp (cDvb* dvb, cBlockPool* blockPool);
  ~cDvbRtp();

  uint64_t getNumPackets();
  uint64_t getNumInvalids();
  uint64_t getNumDiscontinuities();
  uint64_t getNumErrors();

  static bool setOutput (const std::string& outputString, int sid);
  static void processBlockList (cBlock* blockList);
  };
