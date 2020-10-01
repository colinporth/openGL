// simpleTv.cpp - console only tv grab
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <string>
#include <thread>

#include "../shared/utils/cLog.h"
#include "../shared/utils/utils.h"
#include "../shared/dvb/cLinuxDvb.h"

using namespace std;


int main (int argc, char** argv) {

  bool moreLogInfo = false;
  unsigned int frequency = 626;

  if (argc == 1)
    cLog::log (LOGNOTICE, "tune l=loginfo3  itv  bbc  hd  f=freq");
  else // parse params
    for (int arg = 1; arg < argc; arg++)
      if (!strcmp (argv[arg], "l")) moreLogInfo = true;
      else if (!strcmp (argv[arg], "f")) frequency = atoi (argv[++arg]);
      else if (!strcmp (argv[arg], "hd"))  frequency = 626;
      else if (!strcmp (argv[arg], "itv")) frequency = 650;
      else if (!strcmp (argv[arg], "bbc")) frequency = 674;

  cLog::init (moreLogInfo ? LOGINFO3 : LOGINFO, false, "");
  cLog::log (LOGNOTICE, "tv - log:" + dec(logInfo) + " frequency:" + dec(frequency));

  auto dvb = new cDvb (frequency, "/home/pi/ts", true);

  thread captureThread;
  captureThread = thread ([=]() { dvb->captureThread(); });
  sched_param sch_params;
  sch_params.sched_priority = sched_get_priority_max (SCHED_RR);
  pthread_setschedparam (captureThread.native_handle(), SCHED_RR, &sch_params);
  captureThread.detach();

  dvb->grabThread();

  return (0);
  }
