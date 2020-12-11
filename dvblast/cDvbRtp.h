// cDvbRtp.h - dodgy singleton
#pragma once
//{{{  includes
#include <cstdlib>
#include <cstdint>
#include <string>

struct cTsBlock;
class cDvb;
//}}}

class cDvbRtp {
public:
  cDvbRtp (cDvb* dvb, cTsBlockPool* blockPool);
  ~cDvbRtp();

  uint64_t getNumPackets();
  uint64_t getNumErrors();
  uint64_t getNumInvalids();
  uint64_t getNumDiscontinuities();

  static bool setOutput (const std::string& outputString, int sid);
  static void processBlockList (cTsBlock* blockList);
  };
