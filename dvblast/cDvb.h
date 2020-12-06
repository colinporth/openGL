// dvb.h - dodgy singleton
#pragma once
//{{{  includes
#include <cstdlib>
#include <cstdint>
#include <functional>

#include <poll.h>
#include <linux/dvb/frontend.h>

struct cBlock;
//}}}

class cDvb {
public:
  cDvb (int frequency, int adapter);
  ~cDvb();

  cBlock* read (cBlockPool* blockPool);

  void reset();
  int setFilter (uint16_t pid);
  void unsetFilter (int fd, uint16_t pid);

private:
  fe_hierarchy_t getHierarchy();
  fe_guard_interval_t getGuard();
  fe_transmit_mode_t getTransmission();
  fe_spectral_inversion_t getInversion();
  fe_code_rate_t getFEC (fe_caps_t fe_caps, int fecValue);
  fe_delivery_system_t frontendGuessSystem (fe_delivery_system_t* systems, int numSystems);

  void frontendInfo (struct dvb_frontend_info& info, uint32_t version,
                     fe_delivery_system_t* systems, int numSystems);
  void frontendSetup();
  bool frontendStatus();

  int mFrequency = 626000000;
  int mAdapter = 0;
  int mBandwidth = 8;
  int mFeNum = 0;
  int mInversion = -1;
  int mFec = 999;
  int mFecLp = 999;
  int mGuard = -1;
  int mTransmission = -1;
  int mHierarchy = -1;

  fe_status_t mLastStatus;

  int mFrontend = -1;
  struct pollfd fds[1];

  cBlock* mBlockFreeList = NULL;
  };
