// cApp.cpp - dvblast gui
//{{{  includes
#include "cBlockPool.h"
#include "cDvbRtp.h"
#include "cDvb.h"

#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

#include <chrono>
#include <thread>

#include "../../shared/vg/cGlWindow.h"
#include "../../shared/resources/FreeSansBold.h"
#include "../../shared/resources/DroidSansMono.h"

#include "../../shared/fmt/core.h"
#include "../../shared/utils/cLog.h"
#include "../../shared/date/date.h"

#include "../../shared/vg/cGlWindow.h"
#include "../../shared/widgets/cRootContainer.h"
#include "../../shared/widgets/cContainer.h"
#include "../../shared/widgets/cTextBox.h"

using namespace std;
using namespace fmt;
//}}}
//{{{  const multiplexes
struct sMultiplex {
  string mName;
  int mFrequency;
  vector <string> mSelectedChannels;
  vector <string> mSaveNames;
  };

const sMultiplex kHdMultiplex = {
  "hd", 626,
  { "BBC ONE HD", "BBC TWO HD", "ITV HD", "Channel 4 HD", "Channel 5 HD" },
  { "bbc1hd",     "bbc2hd",     "itv1hd", "chn4hd",       "chn5hd" }
  };

const sMultiplex kItvMultiplex = {
  "itv", 650,
  { "ITV",  "ITV2", "ITV3", "ITV4", "Channel 4", "Channel 4+1", "More 4", "Film4" , "E4", "Channel 5" },
  { "itv1", "itv2", "itv3", "itv4", "chn4"     , "c4+1",        "more4",  "film4",  "e4", "chn5" }
  };

const sMultiplex kBbcMultiplex = {
  "bbc", 674,
  { "BBC ONE S West", "BBC TWO", "BBC FOUR" },
  { "bbc1",           "bbc2",    "bbc4" }
  };

const sMultiplex kAllMultiplex = {
  "all", 0,
  { "all" },
  { "" }
  };

struct sMultiplexes {
  vector <sMultiplex> mMultiplexes;
  };
const sMultiplexes kMultiplexes = { { kHdMultiplex, kItvMultiplex, kBbcMultiplex } };
//}}}

// cAppWindow
class cApp : public cGlWindow {
public:
  //{{{
  void run (const string& title, int width, int height, bool gui, bool all, sMultiplex multiplex) {

    if (gui) {
      initialiseGui (title, width, height, (unsigned char*)droidSansMono, sizeof(droidSansMono));
      add (new cTextBox (mString, 0.f));

      thread ([=](){ dvblast(); } ).detach();

      runGui (true);
      }
    else // run in this main thread
      dvblast();

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
  void dvblast() {

    // set thread realtime priority
    struct sched_param param;
    param.sched_priority = sched_get_priority_max (SCHED_RR);
    int error = pthread_setschedparam (pthread_self(), SCHED_RR, &param);
    if (error)
      cLog::log (LOGERROR, "dvblast - pthread_setschedparam failed: %s", strerror (error));

    // init blockPool
    cBlockPool blockPool (100);

    // init dvb
    cDvb dvb (626000000, 0);

    // init dvbRtp
    cDvbRtp dvbRtp (&dvb, &blockPool);
    dvbRtp.setOutput ("192.168.1.109:5002", 17540);
    dvbRtp.setOutput ("192.168.1.109:5004", 17472);
    dvbRtp.setOutput ("192.168.1.109:5006", 17662);
    dvbRtp.setOutput ("192.168.1.109:5008", 17664);
    dvbRtp.setOutput ("192.168.1.109:5010", 17728);

    while (!mExit) {
      mBlocks++;
      dvbRtp.processBlockList (dvb.read (&blockPool));
      mString = format ("dvblast blocks {} packets {} errors:{}:{}:{}",
                        mBlocks, dvbRtp.getNumPackets(),
                        dvbRtp.getNumInvalids(), dvbRtp.getNumDiscontinuities(), dvbRtp.getNumErrors());
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
  bool all = false;
  bool gui = false;
  eLogLevel logLevel = LOGINFO;
  sMultiplex multiplex = kHdMultiplex;
  string fileName;
  //{{{  parse params to options
  for (size_t i = 0; i < params.size(); i++) {
    // look for named multiplex
    bool multiplexFound = false;
    for (size_t j = 0; j < kMultiplexes.mMultiplexes.size() && !multiplexFound; j++) {
      if (params[i] == kMultiplexes.mMultiplexes[j].mName) {
        multiplex = kMultiplexes.mMultiplexes[j];
        multiplexFound = true;
        }
      }
    if (multiplexFound)
      continue;

    if (params[i] == "all") all = true;
    else if (params[i] == "gui") gui = true;
    else if (params[i] == "log1") logLevel = LOGINFO1;
    else if (params[i] == "log2") logLevel = LOGINFO2;
    else if (params[i] == "log3") logLevel = LOGINFO3;
    else if (params[i] == "freq") {
      //  multiplex frequency all channels
      multiplex = kAllMultiplex;
      multiplex.mFrequency = stoi (params[i++]);
      }

    else if (!params[i].empty()) {
      // fileName
      multiplex.mFrequency = 0;
      fileName = params[i];
      }
    }
  //}}}

  cLog::init (logLevel);
  cLog::log (LOGNOTICE, format ("dvblast - freq:{}", multiplex.mFrequency));

  cApp app;
  app.run ("dvblast", 790, 450, gui, all, multiplex);

  return 0;
  }
