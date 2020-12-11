// cApp.cpp - dvblast console or gui app
//{{{  includes
#include "cTsBlockPool.h"
#include "cDvbRtp.h"
#include "cDvb.h"

#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

#include <chrono>
#include <thread>

#include "../../shared/fmt/core.h"
#include "../../shared/utils/cLog.h"

#include "../../shared/vg/cGlWindow.h"
#include "../../shared/resources/DroidSansMono.h"

#include "../../shared/vg/cGlWindow.h"
#include "../../shared/widgets/cRootContainer.h"
#include "../../shared/widgets/cContainer.h"
#include "../../shared/widgets/cTextBox.h"

using namespace std;
using namespace fmt;
//}}}

// cAppWindow
class cApp : public cGlWindow {
public:
  //{{{
  void run (const string& title, int width, int height, int frequency, bool gui, bool multicast) {

    if (gui) {
      initialiseGui (title, width, height, (unsigned char*)droidSansMono, sizeof(droidSansMono));
      add (new cTextBox (mString, 0.f));

      thread ([=](){ dvblast (frequency, multicast, false); } ).detach();

      runGui (true);
      }
    else // run in this main thread
      dvblast (frequency, multicast, true);

    cLog::log (LOGINFO, "exit");
    }
  //}}}
protected:
  //{{{
  void onKey (int key, int scancode, int action, int mods) {

    //mods == GLFW_MOD_SHIFT
    //mods == GLFW_MOD_CONTROL
    if ((action == GLFW_PRESS) || (action == GLFW_REPEAT)) {
      switch (key) {
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
        case GLFW_KEY_P: togglePerf();  break;
        case GLFW_KEY_S: toggleStats(); break;
        case GLFW_KEY_T: toggleTests(); break;

        case GLFW_KEY_I: toggleSolid(); break;
        case GLFW_KEY_A: toggleEdges(); break;
        case GLFW_KEY_Q: setFringeWidth (getFringeWidth() - 0.25f); break;
        case GLFW_KEY_W: setFringeWidth (getFringeWidth() + 0.25f); break;

        case GLFW_KEY_D: toggleDebug(); break;
        case GLFW_KEY_L: cLog::cycleLogLevel(); break;

        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose (mWindow, GL_TRUE); break;
        default: cLog::log (LOGNOTICE, "Keyboard %x", key); break;
        }
      }
    }
  //}}}
private:
  //{{{
  void dvblast (int frequency, bool multicast, bool console) {

    // set thread realtime priority
    struct sched_param param;
    param.sched_priority = sched_get_priority_max (SCHED_RR);
    int error = pthread_setschedparam (pthread_self(), SCHED_RR, &param);
    if (error)
      cLog::log (LOGERROR, "dvblast - pthread_setschedparam failed: %s", strerror (error));

    // init blockPool
    cTsBlockPool blockPool (100);

    // init dvb
    cDvb dvb (frequency, 0);

    // init dvbRtp
    cDvbRtp dvbRtp (&dvb, &blockPool);
    if (multicast) {
      dvbRtp.setOutput ("239.255.1.1:5002", 17540);
      dvbRtp.setOutput ("239.255.1.2:5002", 17472);
      dvbRtp.setOutput ("239.255.1.3:5002", 17662);
      dvbRtp.setOutput ("239.255.1.4:5002", 17664);
      dvbRtp.setOutput ("239.255.1.5:5002", 17728);
      }
    else {
      dvbRtp.setOutput ("192.168.1.109:5002", 17540);
      dvbRtp.setOutput ("192.168.1.109:5004", 17472);
      dvbRtp.setOutput ("192.168.1.109:5006", 17662);
      dvbRtp.setOutput ("192.168.1.109:5008", 17664);
      dvbRtp.setOutput ("192.168.1.109:5010", 17728);
      }
    while (!mExit) {
      mBlocks++;
      dvbRtp.processBlockList (dvb.read (&blockPool));
      mString = format ("dvblast blocks {} packets {} errors:{}:{}:{}",
                        mBlocks, dvbRtp.getNumPackets(),
                        dvbRtp.getNumInvalids(), dvbRtp.getNumDiscontinuities(), dvbRtp.getNumErrors());
      if (console)
        printf ("%s\r", mString.c_str());
      }
    }
  //}}}
  //{{{  vars
  bool mExit = false;

  int mBlocks = 0;
  string mString = "hello";
  //}}}
  };

// main
int main (int numArgs, char* args[]) {
  //{{{  args to params vector<string>
  vector <string> params;
  for (int i = 1; i < numArgs; i++)
    params.push_back (args[i]);
  //}}}

  // options
  bool gui = false;
  bool multicast = false;
  eLogLevel logLevel = LOGINFO;
  //{{{  parse params to options
  for (size_t i = 0; i < params.size(); i++) {
    if (params[i] == "gui") gui = true;
    else if (params[i] == "m") multicast = true;
    else if (params[i] == "log1") logLevel = LOGINFO1;
    else if (params[i] == "log2") logLevel = LOGINFO2;
    else if (params[i] == "log3") logLevel = LOGINFO3;
    }
  //}}}

  cLog::init (logLevel);
  cLog::log (LOGNOTICE, "dvblast");

  cApp app;
  app.run ("dvblast", 790, 450, 626000000, gui, multicast);

  return 0;
  }
