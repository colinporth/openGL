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

#include "../../shared/nanoVg/cGlWindow.h"
#include "../../shared/fonts/FreeSansBold.h"
#include "../../shared/fonts/DroidSansMono1.h"

#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cTransportStreamBox.h"

#include "../../shared/dvb/cDvb.h"

using namespace std;
//}}}

//{{{
struct sMultiplex {
  int mFrequency;
  vector <string> mChannelStrings;
  vector <string> mSaveStrings;
  };
//}}}
//{{{
const sMultiplex kHdMultiplex = { 626,
  { "BBC ONE HD", "BBC TWO HD", "ITV HD", "Channel 4 HD", "Channel 5 HD" },
  { "bbc1hd",     "bbc2hd",     "itv1hd", "chn4hd",       "chn5hd" }
  };
//}}}
//{{{
const sMultiplex kItvMultiplex = { 650,
  { "ITV",  "ITV2", "ITV3", "ITV4", "Channel 4", "More 4", "Film4" , "E4", "Channel 5" },
  { "itv1", "itv2", "itv3", "itv4", "chn4",      "more4",  "film4",  "e4", "chn5" }
  };
//}}}
//{{{
const sMultiplex kBbcMultiplex = { 674,
  { "BBC ONE S West", "BBC TWO", "BBC FOUR" },
  { "bbc1",           "bbc2",    "bbc4" }
   };
//}}}
//{{{
const sMultiplex kAllMultiplex = { 0,
  { "All" },
  { "" }
  };
//}}}

class cAppWindow : public cGlWindow {
public:
  cAppWindow() {}
  //{{{
  void run (const string& title, int width, int height,
            bool headless, bool moreLogInfo, sMultiplex multiplex) {

    mMoreLogInfo = moreLogInfo;

    #ifdef _WIN32
      auto mDvb = new cDvb (multiplex.mFrequency, "/tv", multiplex.mChannelStrings, multiplex.mSaveStrings);
    #else
      auto mDvb = new cDvb (multiplex.mFrequency, "/home/pi/tv", multiplex.mChannelStrings, multiplex.mSaveStrings);
    #endif

    if (!headless) {
      initialise (title, width, height, (unsigned char*)droidSansMono);
      add (new cTextBox (mDvb->mErrorStr, 14.f));
      add (new cTextBox (mDvb->mTuneStr, 12.f));
      add (new cTextBox (mDvb->mSignalStr, 14.f));
      addAt (new cTransportStreamBox (mDvb, 0.f, -2.f), 0.f, 1.f);
      }

    #ifndef _WIN32
      auto captureThread = thread ([=]() { mDvb->captureThread(); });
      sched_param sch_params;
      sch_params.sched_priority = sched_get_priority_max (SCHED_RR);
      pthread_setschedparam (captureThread.native_handle(), SCHED_RR, &sch_params);
      captureThread.detach();
    #endif

    thread ([=]() { mDvb->grabThread(); } ).detach();

    if (headless) {
      while (true)
        this_thread::sleep_for (1s);
      }
    else {
      glClearColor (0, 0, 0, 1.f);
      cGlWindow::run();
      }

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
        case GLFW_KEY_Q: fringeWidth (getFringeWidth() - 0.25f); break;
        case GLFW_KEY_W: fringeWidth (getFringeWidth() + 0.25f); break;

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

  vector <string> argStrings;
  for (int i = 1; i < numArgs; i++)
    argStrings.push_back (args[i]);

  int xWinSize = 790;
  int yWinSize = 400;

  #ifdef _WIN32
    yWinSize = 600;
    CoInitializeEx (NULL, COINIT_MULTITHREADED);
  #endif

  // really dumb option parser
  bool headless = false;
  bool moreLogInfo = false;
  sMultiplex multiplex = kHdMultiplex;
  for (size_t arg = 1; arg < argStrings.size(); arg++)
    if (argStrings[arg] == "h") headless = true;
    else if (argStrings[arg] == "l") moreLogInfo = true;
    else if (argStrings[arg] == "itv") multiplex = kItvMultiplex;
    else if (argStrings[arg] == "bbc") multiplex = kBbcMultiplex;
    else if (argStrings[arg] == "f") {
      //{{{  multiplex with frequency, all channels
      multiplex = kAllMultiplex;
      multiplex.mFrequency = atoi (args[++arg]);
      }
      //}}}

  cLog::init (moreLogInfo ? LOGINFO3 : LOGINFO, false, "");
  cLog::log (LOGNOTICE, "tv - moreLog:" + dec(moreLogInfo) + " freq:" + dec(multiplex.mFrequency));

  cAppWindow appWindow;
  appWindow.run ("tv", xWinSize, yWinSize, headless, moreLogInfo, multiplex);

  // CoUninitialize();
  return 0;
  }

