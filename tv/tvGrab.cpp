// linTvGrab.cpp
//{{{  includes
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include <thread>
#include <string>
#include <chrono>
#include "../../shared/date/date.h"
#include "../../shared/utils/utils.h"
#include "../../shared/utils/cLog.h"

#include "../../shared/nanoVg/cGlWindow.h"
#include "../../shared/fonts/FreeSansBold.h"
#include "../../shared/fonts/DroidSansMono1.h"

#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cTransportStreamBox.h"

#include "../../shared/dvb/cLinuxDvb.h"

using namespace std;
//}}}

class cAppWindow : public cGlWindow {
public:
  cAppWindow() {}
  //{{{
  void run (const string& title, int width, int height, int frequency) {

    cLog::log (LOGINFO, "run %d", frequency);
    mDvb = new cDvb (frequency*1000, "/home/pi/ts", true);

    initialise (title, width, height, (unsigned char*)droidSansMono);
    add (new cTextBox (mDvb->mErrorStr, 12.f));
    add (new cTextBox (mDvb->mTuneStr, 12.f));
    add (new cTextBox (mDvb->mSignalStr, 16.f));
    addAt (new cTransportStreamBox (mDvb, 0.f, -2.f), 0.f, 1.f);

    // launch dvbThread
    auto dvbCaptureThread = thread ([=]() { mDvb->captureThread(); });
    sched_param sch_params;
    sch_params.sched_priority = sched_get_priority_max (SCHED_RR);
    pthread_setschedparam (dvbCaptureThread.native_handle(), SCHED_RR, &sch_params);
    dvbCaptureThread.detach();

    // launch grabThread
    thread ([=]() {
      mDvb->grabThread();
      } ).detach();

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
  bool mLogInfo = false;
  cDvb* mDvb;
  };


int main (int argc, char* argv[]) {

  bool logInfo = false;
  int frequency = 626;

  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) logInfo = true;
    else if (!strcmp (argv[arg], "f")) frequency = atoi (argv[++arg]);
    else if (!strcmp(argv[arg], "itv")) frequency = 650;
    else if (!strcmp(argv[arg], "bbc")) frequency = 674;
    else if (!strcmp(argv[arg], "hd"))  frequency = 626;

  cLog::init (logInfo ? LOGINFO3 : LOGINFO, false, "");
  cLog::log (LOGNOTICE, "tv logInfo:" + dec(logInfo) + " freq:" + dec(frequency));

  cAppWindow appWindow;
  appWindow.run ("tv", 790, 400, frequency);

  return 0;
  }
