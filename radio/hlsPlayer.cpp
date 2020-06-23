// linux hlsPlayer.cpp
//{{{  includes
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

#include "../../shared/utils/cLinuxAudio.h"
#include "../../shared/net/cLinuxHttp.h"

#include "../../shared/widgets/cRootContainer.h"
#include "../../shared/widgets/cListWidget.h"
#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cValueBox.h"
#include "../../shared/widgets/cSelectText.h"
#include "../../shared/widgets/cDecodePicWidget.h"
#include "../../shared/widgets/cNumBox.h"

using namespace std;
#include "../../shared/hls/hls.h"
#include "../../shared/hls/hlsWidgets.h"
//}}}

class cAppWindow : public cHls, public cGlWindow {
public:
  //{{{
  cAppWindow (int chan)
    : cHls (chan, kDefaultBitrate, kBst) {}
  //}}}
  //{{{
  void run (string title, int width, int height) {

    cLog::log (LOGINFO, "run hlsChan:%d", mChan);

    auto root = cGlWindow::initialise (title, width, height, (unsigned char*)freeSansBold);
    hlsMenu (root, this);

    // launch loaderThread
    thread ([=]() { cLinuxHttp http; loader(http); } ).detach();

    // launch playerThread, higher priority
    thread ([=]() {
      cLinuxAudio audio (2, 48000);
      player (audio, this);
      }).detach();

    glClearColor (0, 0, 0, 1.f);
    cGlWindow::run();

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
          mLogInfo = ! mLogInfo;
          cLog::setLogLevel (mLogInfo ? LOGINFO3 : LOGNOTICE);
          break;

        default: cLog::log (LOGNOTICE, "Keyboard %x", key); break;
        }
      }
    }
  //}}}
  void onChar (char ch, int mods) {}

private:
  bool mLogInfo = true;
  };


int main (int argc, char* argv[]) {

  bool logInfo = false;
  uint32_t chan = kDefaultChan;

  for (auto arg = 1; arg < argc; arg++) {
    if (!strcmp(argv[arg], "l")) logInfo = true;
    else if (!strcmp(argv[arg], "1")) chan = 1;
    else if (!strcmp(argv[arg], "2")) chan = 2;
    else if (!strcmp(argv[arg], "3")) chan = 3;
    else if (!strcmp(argv[arg], "4")) chan = 4;
    else if (!strcmp(argv[arg], "5")) chan = 5;
    else if (!strcmp(argv[arg], "6")) chan = 6;
    }

  cLog::init (logInfo ? LOGINFO : LOGINFO3, false, "");
  cLog::log (LOGNOTICE, "linHlsPlayer");

  cAppWindow appWindow (chan);
  appWindow.run ("hls", 480, 272);

  return 0;
  }
