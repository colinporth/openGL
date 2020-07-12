// radio.cpp
//{{{  includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <pthread.h>

#include <thread>
#include <map>
#include <chrono>
#include "../shared/utils/date.h"

#include "../shared/nanoVg/cRaspWindow.h" // early for bigMalloc defines
#include "../shared/utils/utils.h"
#include "../shared/utils/cLog.h"
#include "../shared/utils/cSemaphore.h"
#include "../shared/utils/cKeyboard.h"

#include "../shared/utils/cLinuxAudio.h"
#include "../shared/net/cLinuxHttp.h"

#include "../shared/widgets/cListWidget.h"
#include "../shared/widgets/cTextBox.h"
#include "../shared/widgets/cValueBox.h"
#include "../shared/widgets/cSelectText.h"
#include "../shared/widgets/cPicWidget.h"
#include "../shared/widgets/cDecodePicWidget.h"
#include "../shared/widgets/cNumBox.h"

using namespace std;
using namespace chrono;

#include "../shared/hls/hls.h"
#include "../shared/hls/hlsWidgets.h"
//}}}

class cAppWindow : public cHls, public cRaspWindow, public cLinuxAudio {
public:
  //{{{
  cAppWindow (int chan, int bitrate) :
      cHls (chan, bitrate, 0) {

    mKeyboard.setKeymap (cKeyConfig::getKeymap());
    thread ([=]() { mKeyboard.run(); } ).detach();
    }
  //}}}
  //{{{
  void run (bool windowed, float scale, int alpha) {

    cRootContainer* root = nullptr;
    if (windowed) {
      root = cRaspWindow::initialise (scale, alpha);
      hlsMenu (root, this);
      }

    thread ([=]() { cLinuxHttp http; loader(http); } ).detach();
    thread ([=]() { player (this, this); } ).detach();

    if (root)
      cRaspWindow::run();
    else
      while (true)
        sleep (1);
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
    static map<int, int> getKeymap() {
      map<int,int> keymap;

      keymap[KEY_ESC] = ACT_EXIT;

      keymap[' '] = ACT_PLAYPAUSE;

      keymap['l'] = ACT_LOG;
      keymap['L'] = ACT_LOG;

      keymap[KEY_LEFT]  = ACT_DEC_SEC;
      keymap[KEY_RIGHT] = ACT_INC_SEC;

      keymap[KEY_UP]    = ACT_DEC_CHAN;
      keymap[KEY_DOWN]  = ACT_INC_CHAN;

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
      case cKeyConfig::ACT_PLAYPAUSE: togglePlay(); break;

      case cKeyConfig::ACT_DEC_SEC : incPlaySeconds (-1); break;
      case cKeyConfig::ACT_INC_SEC : incPlaySeconds (1); break;

      case cKeyConfig::ACT_DEC_CHAN:
        //{{{  dec chan
        if (mChan > 1) {
          mChan--;
          mChanChanged = true;
          }
        break;
        //}}}
      case cKeyConfig::ACT_INC_CHAN:
        //{{{  inc chan
        if (mChan < 6) {
          mChan++;
          mChanChanged = true;
          }
        break;
        //}}}
      case cKeyConfig::ACT_CHAN_1: mChan = 1; mChanChanged = true; break;
      case cKeyConfig::ACT_CHAN_2: mChan = 2; mChanChanged = true; break;
      case cKeyConfig::ACT_CHAN_3: mChan = 3; mChanChanged = true; break;
      case cKeyConfig::ACT_CHAN_4: mChan = 4; mChanChanged = true; break;
      case cKeyConfig::ACT_CHAN_5: mChan = 5; mChanChanged = true; break;
      case cKeyConfig::ACT_CHAN_6: mChan = 6; mChanChanged = true; break;

      case cKeyConfig::ACT_TOGGLE_VSYNC: toggleVsync(); changed(); break;
      case cKeyConfig::ACT_TOGGLE_PERF:  togglePerf(); changed();  break;
      case cKeyConfig::ACT_TOGGLE_STATS: toggleStats(); changed(); break;
      case cKeyConfig::ACT_TOGGLE_TESTS: toggleTests(); changed(); break;

      case cKeyConfig::ACT_TOGGLE_SOLID: toggleSolid(); changed(); break;
      case cKeyConfig::ACT_TOGGLE_EDGES: toggleEdges(); changed(); break;
      case cKeyConfig::ACT_TOGGLE_TRIANGLES: toggleTriangles(); changed();  break;   // d
      case cKeyConfig::ACT_LESS_FRINGE: fringeWidth (getFringeWidth() - 0.25f); changed(); break;
      case cKeyConfig::ACT_MORE_FRINGE: fringeWidth (getFringeWidth() + 0.25f); changed(); break;

      case cKeyConfig::ACT_LOG :
        //{{{  set log level
        mLogInfo = ! mLogInfo;
        cLog::setLogLevel (mLogInfo ? LOGINFO3 : LOGNOTICE);
        changed();
        break;
        //}}}

      case cKeyConfig::ACT_EXIT: mExit = true; break;
      }
    }
  //}}}

private:
  cKeyboard mKeyboard;
  bool mLogInfo = true;
  };

// main
int main (int argc, char* argv[]) {

  bool logInfo = false;
  bool windowed = true;
  uint32_t chan = kDefaultChan;
  uint32_t bitrate = kDefaultBitrate;
  uint32_t alpha = 255;
  float scale = 1.f;

  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) logInfo = true;
    else if (!strcmp(argv[arg], "w")) windowed = false;
    else if (!strcmp(argv[arg], "1")) chan = 1;
    else if (!strcmp(argv[arg], "2")) chan = 2;
    else if (!strcmp(argv[arg], "3")) chan = 3;
    else if (!strcmp(argv[arg], "4")) chan = 4;
    else if (!strcmp(argv[arg], "5")) chan = 5;
    else if (!strcmp(argv[arg], "6")) chan = 6;
    else if (!strcmp(argv[arg], "b")) bitrate = 320000;
    else if (!strcmp(argv[arg], "a")) alpha = atoi (argv[++arg]);
    else if (!strcmp(argv[arg], "s")) scale = atoi (argv[++arg]) / 100.f;

  cLog::init (logInfo ? LOGINFO : LOGINFO3, false, "");
  cLog::log (LOGNOTICE, "piRadio log:" + dec(logInfo) +
                        " windowed:" + dec(windowed) +
                        " chan:" + dec(chan) +
                        " bitrate:" + dec(bitrate) +
                        " alpha:" + dec(alpha) +
                        " scale:" + dec(scale));

  cAppWindow appWindow (chan, bitrate);
  appWindow.run (windowed, scale, alpha);

  return 0;
  }
