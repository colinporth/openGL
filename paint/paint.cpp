// main.cpp - tv
//{{{  includes
#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX

  #include <windows.h>
  #include <winsock2.h>
  #include <WS2tcpip.h>
  #include <objbase.h>

  #define YSIZE 600
#else
  #define YSIZE 480
  const int COINIT_MULTITHREADED = 0;

  void CoInitializeEx (void*, int) {}
  void CoUninitialize() {}
#endif

#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include <string>
#include <thread>
#include <chrono>

#include "../../shared/date/date.h"
#include "../../shared/utils/utils.h"
#include "../../shared/utils/cLog.h"

#include "../../shared/dvb/cDvb.h"

#include "../../shared/vg/cGlWindow.h"
#include "../../shared/resources/FreeSansBold.h"
#include "../../shared/resources/DroidSansMono.h"

#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cDvbWidget.h"

using namespace std;
//}}}
//{{{  multiplexes
struct sMultiplex {
  string mName;
  int mFrequency;
  vector <string> mSelectedChannels;
  vector <string> mSaveNames;
  };

const sMultiplex kHdMultiplex = {
  "hd",
  626,
  { "BBC ONE HD", "BBC TWO HD", "ITV HD", "Channel 4 HD", "Channel 5 HD" },
  { "bbc1hd",     "bbc2hd",     "itv1hd", "chn4hd",       "chn5hd" }
  };

const sMultiplex kItvMultiplex = {
  "itv",
  650,
  { "ITV",  "ITV2", "ITV3", "ITV4", "Channel 4", "Channel 4+1", "More 4", "Film4" , "E4", "Channel 5" },
  { "itv1", "itv2", "itv3", "itv4", "chn4"     , "c4+1",        "more4",  "film4",  "e4", "chn5" }
  };

const sMultiplex kBbcMultiplex = {
  "bbc",
  674,
  { "BBC ONE S West", "BBC TWO", "BBC FOUR" },
  { "bbc1",           "bbc2",    "bbc4" }
  };

const sMultiplex kAllMultiplex = {
  "all",
  0,
  { "all" },
  { "" }
  };

struct sMultiplexes {
  vector <sMultiplex> mMultiplexes;
  };
const sMultiplexes kMultiplexes = { { kHdMultiplex, kItvMultiplex, kBbcMultiplex } };
//}}}

class cAppWindow : public cGlWindow {
public:
  cAppWindow() {}
  //{{{
  void run (const string& title, int width, int height,
            bool headless, bool moreLogInfo, bool all,
            sMultiplex multiplex, const string& fileName) {

    mMoreLogInfo = moreLogInfo;

    #ifdef _WIN32
      const string kRootName = "/tv";
    #else
      const string kRootName = "/home/pi/tv";
    #endif

    auto mDvb = new cDvb (multiplex.mFrequency, kRootName, multiplex.mSelectedChannels, multiplex.mSaveNames, false);

   if (!headless) {
      initialise (title, width, height, (unsigned char*)droidSansMono, sizeof(droidSansMono));
      add (new cTextBox (mDvb->mErrorStr, 15.f));
      add (new cTextBox (mDvb->mTuneStr, 12.f));
      add (new cTextBox (mDvb->mSignalStr, 14.f));
      }

    if (fileName.empty()) {
      #ifndef _WIN32
        auto captureThread = thread ([=]() { mDvb->captureThread(); });
        sched_param sch_params;
        sch_params.sched_priority = sched_get_priority_max (SCHED_RR);
        pthread_setschedparam (captureThread.native_handle(), SCHED_RR, &sch_params);
        captureThread.detach();
      #endif

      thread ([=]() { mDvb->grabThread (all ? kRootName : "", multiplex.mName); } ).detach();
      }
    else
      thread ([=]() { mDvb->readThread (fileName); } ).detach();

    if (headless) {
      while (true)
        this_thread::sleep_for (1s);
      }
    else
      runGui (true);

    delete mDvb;

    cLog::log (LOGINFO, "run exit");
    }
  //}}}

protected:
  //{{{
  void onKey (int key, int scancode, int action, int mods) {

    //mods == GLFW_MOD_SHIFT
    //mods == GLFW_MOD_CONTROL
    if ((action == GLFW_PRESS) || (action == GLFW_REPEAT)) {
      switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose (mWindow, GL_TRUE); break;
        case GLFW_KEY_SPACE : break;
        case GLFW_KEY_1:
        case GLFW_KEY_2:
        case GLFW_KEY_3:
        case GLFW_KEY_4:
        case GLFW_KEY_5:
        case GLFW_KEY_6: break;

        case GLFW_KEY_RIGHT: break;
        case GLFW_KEY_LEFT:  break;
        case GLFW_KEY_DOWN:  break;
        case GLFW_KEY_UP:    break;
        case GLFW_KEY_PAGE_UP:   break;
        case GLFW_KEY_PAGE_DOWN: break;
        //case GLFW_KEY_HOME:
        //case GLFW_KEY_END:

        case GLFW_KEY_V: toggleVsync(); break;
        case GLFW_KEY_P: togglePerf(); break;
        case GLFW_KEY_S: toggleStats(); break;
        case GLFW_KEY_T: toggleTests(); break;

        case GLFW_KEY_I: toggleSolid(); break;
        case GLFW_KEY_A: toggleEdges(); break;
        case GLFW_KEY_Q: setFringeWidth (getFringeWidth() - 0.25f); break;
        case GLFW_KEY_W: setFringeWidth (getFringeWidth() + 0.25f); break;

        case GLFW_KEY_L:
          mMoreLogInfo = ! mMoreLogInfo;
          cLog::setLogLevel (mMoreLogInfo ? LOGINFO3 : LOGNOTICE);
          break;

        default: cLog::log (LOGNOTICE, "Keyboard %x", key); break;
        }
      }
    }
  //}}}
  void onChar (char ch, int mods) {}

private:
  bool mMoreLogInfo = false;
  };

int main (int numArgs, char* args[]) {

  CoInitializeEx (NULL, COINIT_MULTITHREADED);
  cLog::init();

  vector <string> argStrings;
  for (int i = 1; i < numArgs; i++)
    argStrings.push_back (args[i]);

  // really dumb option parser
  bool all = false;
  bool headless = false;
  bool moreLogInfo = false;
  sMultiplex multiplex = kHdMultiplex;
  string fileName;

  for (size_t i = 0; i < argStrings.size(); i++) {
    //{{{  look for named multiplex
    bool multiplexFound = false;

    for (size_t j = 0; j < kMultiplexes.mMultiplexes.size() && !multiplexFound; j++) {
      if (argStrings[i] == kMultiplexes.mMultiplexes[j].mName) {
        multiplex = kMultiplexes.mMultiplexes[j];
        multiplexFound = true;
        }
      }

    if (multiplexFound)
      continue;
    //}}}
    if (argStrings[i] == "all") all = true;
    else if (argStrings[i] == "h") headless = true;
    else if (argStrings[i] == "l") moreLogInfo = true;
    else if (argStrings[i] == "f") {
      //{{{  multiplex frequency all channels
      multiplex = kAllMultiplex;
      multiplex.mFrequency = stoi (argStrings[i++]);
      }
      //}}}
    else if (!argStrings[i].empty()) {
      //{{{  fileName
      multiplex.mFrequency = 0;
      fileName = argStrings[i];
      }
      //}}}
    }

  cLog::log (LOGNOTICE, "tv - moreLog:" + dec(moreLogInfo) + " freq:" + dec(multiplex.mFrequency));
  if (moreLogInfo)
    cLog::setLogLevel (LOGINFO3);

  cAppWindow appWindow;
  appWindow.run ("tv", 790, YSIZE, headless, moreLogInfo, all, multiplex, fileName);

  return 0;
  }

