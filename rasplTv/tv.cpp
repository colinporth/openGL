// tv.cpp
//{{{  includes
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include <thread>
#include <chrono>
#include <string>

#include "../shared/utils/date.h"
#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "../shared/utils/cSemaphore.h"
#include "../shared/utils/cKeyboard.h"

#include "../shared/dvb/cLinuxDvb.h"
#include "../shared/nanoVg/cRaspWindow.h"

#include "../shared/widgets/cTextBox.h"
#include "../shared/widgets/cValueBox.h"
#include "../shared/widgets/cSelectText.h"
#include "../shared/widgets/cNumBox.h"
#include "../shared/widgets/cTransportStreamBox.h"

using namespace std;
//}}}

class cAppWindow : public cRaspWindow {
public:
  //{{{
  cAppWindow() {

    // config and launch keyboardThread
    mKeyboard.setKeymap (cKeyConfig::getKeymap());
    thread ([=]() { mKeyboard.run(); } ).detach();
    }
  //}}}
  //{{{
  void run (int frequency, const string& inTs) {

    mDvb = new cDvb (frequency, "/home/pi/ts", true);

    initialise (1.0, 1.0f);
    setChangeCountDown (4);
    add (new cTextBox (mDvb->mErrorStr, 13.f));
    add (new cTextBox (mDvb->mSignalStr, 13.f));
    add (new cTextBox (mDvb->mTuneStr, 13.f));
    add (new cTransportStreamBox (mDvb, 0.f, -1.f));

    thread dvbCaptureThread;
    if (inTs.empty()) {
      // launch dvbThread
      dvbCaptureThread = thread ([=]() { mDvb->captureThread(); });
      sched_param sch_params;
      sch_params.sched_priority = sched_get_priority_max (SCHED_RR);
      pthread_setschedparam (dvbCaptureThread.native_handle(), SCHED_RR, &sch_params);
      dvbCaptureThread.detach();

      thread ([=]() { mDvb->grabThread(); } ).detach();
      }
    else {
      thread ([=]() { readThread (inTs); } ).detach();
      }

    cRaspWindow::run();

    delete mDvb;
    }
  //}}}

protected:
  //{{{
  class cKeyConfig {
  public:
    //{{{  keys
    #define KEY_ENTER    0x0a
    #define KEY_ESC      0x1b

    #define KEY_HOME     0x317e
    #define KEY_INSERT   0x327e
    #define KEY_DELETE   0x337e
    #define KEY_END      0x347e
    #define KEY_PAGEUP   0x357e
    #define KEY_PAGEDOWN 0x367e

    #define KEY_UP       0x5b41
    #define KEY_DOWN     0x5b42
    #define KEY_RIGHT    0x5b43
    #define KEY_LEFT     0x5b44
    //}}}

    enum eKeyAction {
      ACT_NONE, ACT_EXIT,
      ACT_PLAYPAUSE, ACT_LOG,
      ACT_DEC_CHAN, ACT_INC_CHAN,
      ACT_INC_SEC, ACT_DEC_SEC,
      ACT_CHAN_1, ACT_CHAN_2, ACT_CHAN_3, ACT_CHAN_4, ACT_CHAN_5, ACT_CHAN_6,
      ACT_TOGGLE_VSYNC, ACT_TOGGLE_PERF,
      ACT_TOGGLE_STATS, ACT_TOGGLE_TESTS,
      ACT_TOGGLE_SOLID, ACT_TOGGLE_EDGES, ACT_TOGGLE_TRIANGLES,
      ACT_LESS_FRINGE, ACT_MORE_FRINGE
      };

    //{{{
    static map<int,int> getKeymap() {
      map<int,int> keymap;

      keymap[KEY_ESC] = ACT_EXIT;

      keymap['l'] = ACT_LOG;
      keymap['L'] = ACT_LOG;

      keymap[' '] = ACT_PLAYPAUSE;

      keymap[KEY_LEFT]  = ACT_DEC_SEC;
      keymap[KEY_RIGHT] = ACT_INC_SEC;

      keymap[KEY_UP]   = ACT_DEC_CHAN;
      keymap[KEY_DOWN] = ACT_INC_CHAN;

      keymap['1'] = ACT_CHAN_1;
      keymap['2'] = ACT_CHAN_2;
      keymap['3'] = ACT_CHAN_3;
      keymap['4'] = ACT_CHAN_4;
      keymap['5'] = ACT_CHAN_5;
      keymap['6'] = ACT_CHAN_6;

      keymap['v'] = ACT_TOGGLE_VSYNC;
      keymap['V'] = ACT_TOGGLE_VSYNC;
      keymap['p'] = ACT_TOGGLE_PERF;
      keymap['P'] = ACT_TOGGLE_PERF;
      keymap['t'] = ACT_TOGGLE_STATS;
      keymap['T'] = ACT_TOGGLE_STATS;
      keymap['e'] = ACT_TOGGLE_TESTS;
      keymap['E'] = ACT_TOGGLE_TESTS;

      keymap['s'] = ACT_TOGGLE_SOLID;
      keymap['S'] = ACT_TOGGLE_SOLID;
      keymap['a'] = ACT_TOGGLE_EDGES;
      keymap['A'] = ACT_TOGGLE_EDGES;
      keymap['d'] = ACT_TOGGLE_TRIANGLES;
      keymap['D'] = ACT_TOGGLE_TRIANGLES;
      keymap['q'] = ACT_LESS_FRINGE;
      keymap['Q'] = ACT_LESS_FRINGE;
      keymap['w'] = ACT_MORE_FRINGE;
      keymap['W'] = ACT_MORE_FRINGE;

      return keymap;
      }
    //}}}
    };
  //}}}
  //{{{
  void pollKeyboard() {

    switch (mKeyboard.getEvent()) {
      case cKeyConfig::ACT_PLAYPAUSE: break;

      case cKeyConfig::ACT_DEC_SEC: break;
      case cKeyConfig::ACT_INC_SEC: break;

      case cKeyConfig::ACT_DEC_CHAN: break;
      case cKeyConfig::ACT_INC_CHAN: break;

      case cKeyConfig::ACT_CHAN_1: changed(); break;
      case cKeyConfig::ACT_CHAN_2: changed(); break;
      case cKeyConfig::ACT_CHAN_3: changed(); break;
      case cKeyConfig::ACT_CHAN_4: changed(); break;
      case cKeyConfig::ACT_CHAN_5: changed(); break;
      case cKeyConfig::ACT_CHAN_6: changed(); break;

      case cKeyConfig::ACT_TOGGLE_VSYNC: toggleVsync(); changed(); break; // v
      case cKeyConfig::ACT_TOGGLE_PERF:  togglePerf(); changed();  break; // p
      case cKeyConfig::ACT_TOGGLE_STATS: toggleStats(); changed(); break; // t
      case cKeyConfig::ACT_TOGGLE_TESTS: toggleTests(); changed(); break; // e

      case cKeyConfig::ACT_TOGGLE_SOLID: toggleSolid(); changed(); break; // i
      case cKeyConfig::ACT_TOGGLE_EDGES: toggleEdges(); changed(); break; // a
      case cKeyConfig::ACT_TOGGLE_TRIANGLES: toggleTriangles(); changed();  break; // d
      case cKeyConfig::ACT_LESS_FRINGE:  fringeWidth (getFringeWidth() - 0.25f); changed(); break; // q
      case cKeyConfig::ACT_MORE_FRINGE:  fringeWidth (getFringeWidth() + 0.25f); changed(); break; // w

      case cKeyConfig::ACT_LOG : changed(); break;

      case cKeyConfig::ACT_EXIT: mExit = true; break;
      }
    }
  //}}}

private:
  //{{{
  void readThread (const std::string& inTs) {

    cTransportStream ts;

    cLog::setThreadName ("read");

    auto file = fopen (inTs.c_str(), "rb");
    if (!file) {
      //{{{  error, return
      cLog::log (LOGERROR, "no file " + inTs);
      return;
      }
      //}}}

    uint64_t streamPos = 0;
    auto blockSize = 188 * 8;
    auto buffer = (uint8_t*)malloc (blockSize);

    bool run = true;
    while (run) {
      int bytesRead = fread (buffer, 1, blockSize, file);
      if (bytesRead > 0)
        streamPos += ts.demux (buffer, bytesRead, streamPos, false, -1);
      else
        break;
      //mErrorStr = dec (ts.getDiscontinuity());
      }

    fclose (file);
    free (buffer);

    cLog::log (LOGERROR, "exit");
    }
  //}}}

  cKeyboard mKeyboard;
  cDvb* mDvb;
  };

// main
int main (int argc, char* argv[]) {

  bool logInfo = false;
  int frequency = 626;
  string inTs = "";

  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) logInfo = true;
    else if (!strcmp(argv[arg], "itv")) frequency = 650;
    else if (!strcmp(argv[arg], "bbc")) frequency = 674;
    else if (!strcmp(argv[arg], "hd"))  frequency = 626;
    else if (!strcmp(argv[arg], "i")) inTs = argv[++arg];

  cLog::init (logInfo ? LOGINFO : LOGINFO3, false, "");
  cLog::log (LOGNOTICE, "piTv log:" + dec(logInfo) + " freq:" + dec(frequency));

  cAppWindow appWindow;
  appWindow.run (frequency, inTs);

  return 0;
  }
