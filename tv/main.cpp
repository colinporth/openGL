// main.cpp - tv
//{{{  includes
#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
  #define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
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

#ifdef _WIN32
  #include "../../shared/dvb/cWinDvb.h"
#else
  #include "../../shared/dvb/cLinuxDvb.h"
#endif

using namespace std;
//}}}

class cAppWindow : public cGlWindow {
public:
  cAppWindow() {}
  //{{{
  void run (const string& title, int width, int height, int frequency) {

    cLog::log (LOGINFO, "run %d", frequency);
    mDvb = new cDvb (frequency*1000, "/tv", true);

    initialise (title, width, height, (unsigned char*)droidSansMono);
    add (new cTextBox (mDvb->mErrorStr, 12.f));
    add (new cTextBox (mDvb->mTuneStr, 12.f));
    add (new cTextBox (mDvb->mSignalStr, 16.f));
    addAt (new cTransportStreamBox (mDvb, 0.f, -2.f), 0.f, 1.f);

    #ifdef _WIN32
      thread ([=]() {
        CoInitializeEx (NULL, COINIT_MULTITHREADED);
        mDvb->grabThread();
        CoUninitialize();
        }).detach();

      thread ([=]() {
        CoInitializeEx (NULL, COINIT_MULTITHREADED);
        mDvb->signalThread();
        CoUninitialize();
        }).detach();
    #endif

    glClearColor (0, 0, 0, 1.f);
    cGlWindow::run();

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
  cDvb* mDvb;
  };


int main (int argc, char* argv[]) {

  #ifdef _WIN32
    CoInitializeEx (NULL, COINIT_MULTITHREADED);
  #endif

  bool moreLogInfo = false;
  int frequency = 626;

  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) moreLogInfo = true;
    else if (!strcmp (argv[arg], "f")) frequency = atoi (argv[++arg]);
    else if (!strcmp (argv[arg], "hd"))  frequency = 626;
    else if (!strcmp (argv[arg], "itv")) frequency = 650;
    else if (!strcmp (argv[arg], "bbc")) frequency = 674;

  cLog::init (moreLogInfo ? LOGINFO3 : LOGINFO, false, "");
  cLog::log (LOGNOTICE, "tv - log:" + dec(logInfo) + " freq:" + dec(frequency));

  cAppWindow appWindow;
  #ifdef _WIN32
    appWindow.run ("tv", 800, 480, frequency);
  #else
    appWindow.run ("tv", 790, 400, frequency);
  #endif

  #ifdef _WIN32
    CoUninitialize();
  #endif

  return 0;
  }
