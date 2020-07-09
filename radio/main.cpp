// main.cpp - windows,linux hls radio
//{{{  includes
#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX

  #include <windows.h>
  #include <winsock2.h>
  #include <WS2tcpip.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include <thread>
#include <chrono>
#include "../../shared/date/date.h"

#include "../../shared/nanoVg/cGlWindow.h"
#include "../../shared/fonts/FreeSansBold.h"

#include "../../shared/utils/utils.h"
#include "../../shared/utils/cLog.h"
#include "../../shared/utils/cSemaphore.h"

#ifdef _WIN32
  #include "../../shared/net/cWinSockHttp.h"
  #include "../../shared/utils/cWinAudio16.h"
#else
  #include "../../shared/net/cLinuxHttp.h"
  #include "../../shared/utils/cLinuxAudio.h"
#endif

#include "../../shared/widgets/cValueBox.h"
#include "../../shared/widgets/cSelectText.h"

#include "../../shared/hls/hls.h"
#include "../../shared/hls/hlsWidgets.h"

using namespace std;
//}}}

class cAppWindow : public cHls, public cGlWindow {
public:
  cAppWindow (int chan, int bitrate) : cHls (chan, bitrate, kBst) {}
  //{{{
  void run (const string& title, int width, int height, bool headless, bool moreLogInfo) {

    mMoreLogInfo = moreLogInfo;
    cLog::log (LOGINFO, "run chan:%d bitrate:%d", mChan, mBitrate);

    if (!headless) {
      auto root = cGlWindow::initialise (title, width, height, (unsigned char*)freeSansBold);
      hlsMenu (root, this);
      }

    thread ([=]() { cPlatformHttp http; loader (http); } ).detach();
    thread ([=]() { cAudio16 audio16 (2, 48000); player (audio16, this); } ).detach();

    if (headless) {
      while (true)
        this_thread::sleep_for (1s);
      }
    else {
      glClearColor (0, 0, 0, 1.f);
      cGlWindow::run();
      }

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
        case GLFW_KEY_SPACE : togglePlay(); break;
        case GLFW_KEY_1:
        case GLFW_KEY_2:
        case GLFW_KEY_3:
        case GLFW_KEY_4:
        case GLFW_KEY_5:
        case GLFW_KEY_6: mChan = key - GLFW_KEY_0; mChanChanged = true; break;

        case GLFW_KEY_RIGHT: incPlaySeconds (1); break;
        case GLFW_KEY_LEFT:  incPlaySeconds (-1); break;
        case GLFW_KEY_DOWN:
          //{{{  down arrow
          if (mChan < 6) {
            mChan++;
            mChanChanged = true;
            }
          break;
          //}}}
        case GLFW_KEY_UP:
          //{{{  up arrow
          if (mChan > 1) {
            mChan--;
            mChanChanged = true;
            }
          break;
          //}}}
        case GLFW_KEY_PAGE_UP:   incPlaySeconds (-60); break;
        case GLFW_KEY_PAGE_DOWN: incPlaySeconds (60); break;
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
          mMoreLogInfo = !mMoreLogInfo;
          cLog::setLogLevel (mMoreLogInfo ? LOGINFO3 : LOGINFO);
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


int main (int argc, char* argv[]) {

  #ifdef _WIN32
    CoInitializeEx (NULL, COINIT_MULTITHREADED);
  #endif

  bool headless = false;
  bool moreLogInfo = false;
  uint32_t chan = kDefaultChan;
  uint32_t bitrate = kDefaultBitrate;

  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) moreLogInfo = true;
    else if (!strcmp(argv[arg], "h")) headless = true;
    else if (!strcmp(argv[arg], "b")) bitrate = 320000;
    else if (!strcmp(argv[arg], "1")) chan = 1;
    else if (!strcmp(argv[arg], "2")) chan = 2;
    else if (!strcmp(argv[arg], "3")) chan = 3;
    else if (!strcmp(argv[arg], "4")) chan = 4;
    else if (!strcmp(argv[arg], "5")) chan = 5;
    else if (!strcmp(argv[arg], "6")) chan = 6;

  cLog::init (moreLogInfo ? LOGINFO3 : LOGINFO, false, "");
  cLog::log (LOGNOTICE, "radio " + dec(moreLogInfo) + " chan:" + dec(chan) +
                        " bitrate:" + dec(bitrate) + " headless" + dec(headless));

  cAppWindow appWindow (chan, bitrate);
  #ifdef _WIN32
    appWindow.run ("hls", 800, 480, headless, moreLogInfo);
    CoUninitialize();
  #else
    appWindow.run ("hls", 480, 272, headless, moreLogInfo);
  #endif

  return 0;
  }
