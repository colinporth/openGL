// linTvGrab.cpp
//{{{  includes
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include <thread>
#include <chrono>
//#include <string>
#include "../../shared/utils/date.h"

#include "../../shared/nanoVg/cGlWindow.h"
#include "../../shared/fonts/FreeSansBold.h"
#include "../../shared/fonts/DroidSansMono1.h"

#include "../../shared/utils/utils.h"
#include "../../shared/utils/cLog.h"

#include "../../shared/dvb/cLinuxDvb.h"

#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cValueBox.h"
#include "../../shared/widgets/cSelectText.h"
#include "../../shared/widgets/cNumBox.h"
#include "../../shared/widgets/cTransportStreamBox.h"

using namespace std;
//}}}

class cAppWindow : public cGlWindow {
public:
  cAppWindow() {}
  //{{{
  void run (const string& title, int width, int height, unsigned int frequency, const string& root) {

    initialise (title, width, height, (unsigned char*)droidSansMono);
    add (new cTextBox (mPacketStr, 15.f));
    add (new cTextBox (mDvb.mSignalStr, 14.f));
    add (new cTextBox (mDvb.mTuneStr, 12.f));
    addAt (new cTransportStreamBox (&mDvb, 0.f, -2.f), 0.f, 1.f);

    mDvb = new cDvb (frequency, root);

    // launch dvbThread
    auto dvbCaptureThread = thread ([=]() { mDvb->captureThread (frequency); });
    sched_param sch_params;
    sch_params.sched_priority = sched_get_priority_max (SCHED_RR);
    pthread_setschedparam (dvbCaptureThread.native_handle(), SCHED_RR, &sch_params);
    dvbCaptureThread.detach();

    // launch grabThread
    thread ([=]() { mDvb->grabThread(); } ).detach();

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
  //{{{  vars
  cDvb* mDvb;

  string mStr1 = "tv app";
  string mStr2 = "";
  string mStr3 = "";

  uint64_t mLastDiscontinuity = 0;
  int mLastBlockSize = 0;
  int mMaxBlockSize = 0;
  string mPacketStr = "packet";

  bool mLogInfo = true;
  //}}}
  };


int main (int argc, char* argv[]) {

  bool logInfo = false;
  int frequency = 626;
  string root = "home/pi/ts";

  for (auto arg = 1; arg < argc; arg++)
    if (!strcmp(argv[arg], "l")) logInfo = true;
    else if (!strcmp(argv[arg], "itv")) frequency = 650;
    else if (!strcmp(argv[arg], "bbc")) frequency = 674;
    else if (!strcmp(argv[arg], "hd"))  frequency = 626;
    else if (!strcmp(argv[arg], "r")) root = argv[++arg];

  cLog::init (logInfo ? LOGINFO3 : LOGINFO, false, "");
  cLog::log (LOGNOTICE, "linTvGrab logInfo:" + dec(logInfo) +
                        " freq:" + dec(frequency) +
                        " root:" + root);

  cAppWindow appWindow (root);
  appWindow.run ("tv - openGL2", 790, 400, frequency, root);

  return 0;
  }
