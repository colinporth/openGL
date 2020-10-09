// main.cpp - nanoVg openGL - hls audio/video windows/linux
//{{{  includes
#ifdef _WIN32
  // windows headers, defines
  #define _CRT_SECURE_NO_WARNINGS
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#endif

// c++
#include <cstdint>
#include <string>
#include <thread>
#include <chrono>

// utils
#include "../../shared/date/date.h"
#include "../../shared/utils/utils.h"
#include "../../shared/utils/cLog.h"

// audio container
#include "../../shared/utils/cSong.h"

// video decode
#include "../../shared/utils/cVideoDecode.h"
#include "../../shared/hls/cHlsPlayer.h"

// widgets
#include "../../shared/nanoVg/cGlWindow.h"
#include "../../shared/fonts/FreeSansBold.h"
#include "../../shared/fonts/DroidSansMono1.h"
#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cSongWidget.h"
#include "../../shared/widgets/cHlsPlayerWidget.h"

using namespace std;
using namespace chrono;
//}}}

// channels
const string kHost = "vs-hls-uk-live.akamaized.net";
const vector <string> kChannels = { "bbc_one_hd",          "bbc_two_hd",          "bbc_four_hd", // pa4
                                    "bbc_news_channel_hd", "bbc_one_scotland_hd", "s4cpbs",      // pa4
                                    "bbc_one_south_west",  "bbc_parliament" };                   // pa3
constexpr int kAudBitrate = 128000; // 96000  128000

class cAppWindow : public cGlWindow, public cHlsPlayer {
public:
  cAppWindow() : cHlsPlayer() {}
  //{{{
  void run (const string& title, int width, int height,
            bool headless, bool logInfo3,
            int channelNum, int audBitrate, int vidBitrate)  {

    mLogInfo3 = logInfo3;

    init (kHost, kChannels[channelNum], audBitrate, vidBitrate);

    if (headless) {
      thread ([=](){ loaderThread(); }).detach();
      while (true)
        this_thread::sleep_for (200ms);
       }

    else {
      initialise (title, width, height, (unsigned char*)droidSansMono, sizeof(droidSansMono));
      addTopLeft (new cHlsPlayerWidget (this, 0,0));
      addTopLeft (new cSongWidget (mSong, 0,0));

      thread ([=](){ loaderThread(); }).detach();

      glClearColor (0, 0, 0, 1.f);
      cGlWindow::run();
      }

    cLog::log (LOGINFO, "run exit");
    }
  //}}}

protected:
  void onChar (char ch, int mods) {}
  //{{{
  void onKey (int key, int scancode, int action, int mods) {

    if ((action == GLFW_PRESS) || (action == GLFW_REPEAT)) {
      switch (key) {
        case GLFW_KEY_1:
        case GLFW_KEY_2:
        case GLFW_KEY_3:
        case GLFW_KEY_4:
        case GLFW_KEY_5:
        case GLFW_KEY_6: break;

        //{{{
        case GLFW_KEY_SPACE:     // pause
          mPlaying = !mPlaying;
          break;
        //}}}
        //{{{
        case GLFW_KEY_DELETE:    // delete select
          mSong->getSelect().clearAll();
          //changed();
          break;
        //}}}

        //{{{
        case GLFW_KEY_HOME:      // skip beginning
          mSong->setPlayFrame (mSong->getSelect().empty() ? mSong->getFirstFrame() : mSong->getSelect().getFirstFrame());
          videoFollowAudio();
          break;
        //}}}
        //{{{
        case GLFW_KEY_END:       // skip end
          mSong->setPlayFrame (mSong->getSelect().empty() ? mSong->getLastFrame() : mSong->getSelect().getLastFrame());
          videoFollowAudio();
          break;
        //}}}
        //{{{
        case GLFW_KEY_PAGE_UP:   // nothing
          break;
        //}}}
        //{{{
        case GLFW_KEY_PAGE_DOWN: // nothing
          break;
        //}}}
        //{{{
        case GLFW_KEY_LEFT:      // skip back
          mSong->incPlaySec (-(mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
          videoFollowAudio();
          break;
        //}}}
        //{{{
        case GLFW_KEY_RIGHT:     // skip forward
          mSong->incPlaySec ((mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
          videoFollowAudio();
          break;
        //}}}
        //{{{
        case GLFW_KEY_DOWN:      // nothing
          break;
        //}}}
        //{{{
        case GLFW_KEY_UP:        // nothing
          break;
        //}}}

        //{{{
        case GLFW_KEY_M: // mark
          mSong->getSelect().addMark (mSong->getPlayFrame());
          //changed();
          break;
        //}}}

        //{{{
        case GLFW_KEY_V: // toggle vsync
          toggleVsync();
          break;
        //}}}
        //{{{
        case GLFW_KEY_P: // toggle perf stats
          togglePerf();
          break;
        //}}}
        //{{{
        case GLFW_KEY_S: // toggle vg stats
          toggleStats();
          break;
        //}}}
        //{{{
        case GLFW_KEY_T: // toggle tests
          toggleTests();
          break;
        //}}}

        //{{{
        case GLFW_KEY_I: // toggle solid
          toggleSolid();
          break;
        //}}}
        //{{{
        case GLFW_KEY_A: // toggle edges
          toggleEdges();
          break;
        //}}}
        //{{{
        case GLFW_KEY_Q: // less edge
          fringeWidth (getFringeWidth() - 0.25f);
          break;
        //}}}
        //{{{
        case GLFW_KEY_W: // more edge
          fringeWidth (getFringeWidth() + 0.25f);
          break;
        //}}}

        //{{{
        case GLFW_KEY_L: // toggle log level LOGINFO : LOGINFO3
          mLogInfo3 = ! mLogInfo3;
          cLog::setLogLevel (mLogInfo3 ? LOGINFO3 : LOGINFO);
          break;
        //}}}
        //{{{
        case GLFW_KEY_ESCAPE: // exit
          glfwSetWindowShouldClose (mWindow, GL_TRUE);
          mExit = true;
          break;
        //}}}
        default: cLog::log (LOGNOTICE, "Keyboard %x", key); break;
        }
      }
    }
  //}}}

  //  vars
  bool mLogInfo3 = false;
  };

int main (int numArgs, char* args[]) {

  vector <string> argStrings;
  for (int i = 1; i < numArgs; i++)
    argStrings.push_back (args[i]);

  bool headless = false;
  bool logInfo3 = false;
  int channelNum = 3;
  int vidBitrate = 827008;
  for (size_t i = 0; i < argStrings.size(); i++) {
    //{{{  parse params
    if (argStrings[i] == "h") headless = true;
    else if (argStrings[i] == "l") logInfo3 = true;
    else if (argStrings[i] == "bbc1") channelNum = 0;
    else if (argStrings[i] == "bbc2") channelNum = 1;
    else if (argStrings[i] == "bbc4") channelNum = 2;
    else if (argStrings[i] == "news") channelNum = 3;
    else if (argStrings[i] == "scot") channelNum = 4;
    else if (argStrings[i] == "s4c") channelNum = 5;
    else if (argStrings[i] == "sw") channelNum = 6;
    else if (argStrings[i] == "parl") channelNum = 7;
    else if (argStrings[i] == "v1") vidBitrate = 827008;
    else if (argStrings[i] == "v2") vidBitrate = 1604032;
    else if (argStrings[i] == "v3") vidBitrate = 2812032;
    else if (argStrings[i] == "v4") vidBitrate = 5070016;
    }
    //}}}

  cLog::init (logInfo3 ? LOGINFO3 : LOGINFO);
  cLog::log (LOGNOTICE, "openGL hls " + kChannels[channelNum] + " " +
                        dec (vidBitrate) + " " +
                        string(logInfo3 ? "logInfo3 " : "") +
                        string(headless ? "headless " : ""));

  cAppWindow appWindow;
  appWindow.run ("hls", 800, 450, headless, logInfo3, channelNum, kAudBitrate, vidBitrate);

  return 0;
  }
