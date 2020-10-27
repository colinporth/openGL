// main.cpp - vg openGL - hls audio/video windows/linux
//{{{  includes
#ifdef _WIN32
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

// loader,player
#include "../../shared/utils/cLoaderPlayer.h"

// widgets
#include "../../shared/vg/cGlWindow.h"
#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cImageWidget.h"
#include "../../shared/widgets/cLoaderPlayerWidget.h"
//{{{  include resources
#include "../../shared/resources/bbc1.h"
#include "../../shared/resources/bbc2.h"
#include "../../shared/resources/bbc4.h"
#include "../../shared/resources/bbcnews.h"

#include "../../shared/resources/r1.h"
#include "../../shared/resources/r2.h"
#include "../../shared/resources/r3.h"
#include "../../shared/resources/r4.h"
#include "../../shared/resources/r5.h"
#include "../../shared/resources/r6.h"

#include "../../shared/resources/DroidSansMono.h"
//}}}

using namespace std;
using namespace chrono;
//}}}

// cAppWindow
class cAppWindow : public cLoaderPlayer, public cGlWindow {
public:
  cAppWindow() : cLoaderPlayer() {}
  //{{{
  void run (const string& title, int width, int height, bool headless, eLoaderFlags loaderFlags,
            bool radio, const string& channelName, int audBitrate, int vidBitrate)  {

    if (headless) {
      thread ([=](){ hlsLoaderThread (true, "bbc_radio_fourfm", 128000, 0, loaderFlags); }).detach();
      while (true)
        this_thread::sleep_for (200ms);
      }
    else {
      // start gui
      cGlWindow::initialise (title, width, height, (uint8_t*)droidSansMono, sizeof(droidSansMono));
      addTopLeft (new cLoaderPlayerWidget (this, this, cPointF()));

      if (!channelName.empty())
        // select cmdline channel
        thread ([=](){ hlsLoaderThread (radio, channelName, audBitrate, vidBitrate, loaderFlags); }).detach();
      else {
        // add channel gui
        addTopLeft (new cImageWidget(r1, sizeof(r1), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (true, "bbc_radio_one", 128000,0, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(r2, sizeof(r2), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (true, "bbc_radio_two", 128000,0, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(r3, sizeof(r3), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (true, "bbc_radio_three", 320000,0, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(r4, sizeof(r4), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (true, "bbc_radio_fourfm", 128000,0, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(r5, sizeof(r5), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (true, "bbc_radio_five_live", 128000,0, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(r6, sizeof(r6), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (true, "bbc_6music", 128000,0, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(bbc1, sizeof(bbc1), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (false, "bbc_one_hd", 128000,1604032, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(bbc2, sizeof(bbc2), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (false, "bbc_two_hd", 128000,1604032, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(bbc4, sizeof(bbc4), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (false, "bbc_four_hd", 128000,1604032, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(bbcnews, sizeof(bbcnews), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (false, "bbc_news_channel_hd", 128000,1604032, loaderFlags); }).detach();
          } ));
          //}}}
        add (new cImageWidget(bbc1, sizeof(bbc1), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
          //{{{  lambda
          stopAndWait();
          thread ([=](){ hlsLoaderThread (false, "bbc_one_south_west", 128000,1604032, loaderFlags); }).detach();
          } ));
          //}}}
        }
      cGlWindow::run (false);
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
          getSong()->getSelect().clearAll();
          //changed();
          break;
        //}}}

        //{{{
        case GLFW_KEY_HOME:      // skip beginning
          getSong()->setPlayFrame (getSong()->getSelect().empty() ? getSong()->getFirstFrame() : getSong()->getSelect().getFirstFrame());
          videoFollowAudio();
          break;
        //}}}
        //{{{
        case GLFW_KEY_END:       // skip end
          getSong()->setPlayFrame (getSong()->getSelect().empty() ? getSong()->getLastFrame() : getSong()->getSelect().getLastFrame());
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
          getSong()->incPlaySec (-(mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
          videoFollowAudio();
          break;
        //}}}
        //{{{
        case GLFW_KEY_RIGHT:     // skip forward
          getSong()->incPlaySec ((mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
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
          getSong()->getSelect().addMark (getSong()->getPlayFrame());
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
          setFringeWidth (getFringeWidth() - 0.25f);
          break;
        //}}}
        //{{{
        case GLFW_KEY_W: // more edge
          setFringeWidth (getFringeWidth() + 0.25f);
          break;
        //}}}

        //{{{
        case GLFW_KEY_L:
          cLog::cycleLogLevel();
          break;
        //}}}
        //{{{
        case GLFW_KEY_F: // toggle fullScreen
          toggleFullScreen();
          break;
        //}}}
        //{{{
        case GLFW_KEY_ESCAPE: // exit
          stopAndWait();
          glfwSetWindowShouldClose (mWindow, GL_TRUE);
          break;
        //}}}
        default: cLog::log (LOGNOTICE, "Keyboard %x", key); break;
        }
      }
    }
  //}}}
private:
  //{{{
  void stopAndWait() {

    mExit = true;
    while (mRunning) {
      this_thread::sleep_for (100ms);
      cLog::log(LOGINFO, "waiting to exit");
      }
    }
  //}}}
  };

// main
int main (int numArgs, char* args[]) {
  //{{{  args to strings
  vector <string> argStrings;
  for (int i = 1; i < numArgs; i++)
    argStrings.push_back (args[i]);
  //}}}
  //{{{  default params
  bool headless = false;
  bool forceFFmpeg = true;
  eLogLevel logLevel = LOGINFO;

  int radio = false;
  string channelName;
  int audBitrate = 128000;
  int vidBitrate = 827008;
  //}}}
  for (size_t i = 0; i < argStrings.size(); i++) {
    //{{{  parse params
    if (argStrings[i] == "h") headless = true;
    else if (argStrings[i] == "mfx") forceFFmpeg = false;
    else if (argStrings[i] == "l1") logLevel = LOGINFO1;
    else if (argStrings[i] == "l2") logLevel = LOGINFO2;
    else if (argStrings[i] == "l3") logLevel = LOGINFO3;

    else if (argStrings[i] == "bbc1") channelName = "bbc_one_hd";
    else if (argStrings[i] == "bbc2") channelName = "bbc_two_hd";
    else if (argStrings[i] == "bbc4") channelName = "bbc_four_hd";
    else if (argStrings[i] == "news") channelName = "bbc_news_channel_hd";
    else if (argStrings[i] == "scot") channelName = "bbc_one_scotland_hd";
    else if (argStrings[i] == "s4c") channelName = "s4cpbs";
    else if (argStrings[i] == "sw") channelName = "bbc_one_south_west";
    else if (argStrings[i] == "parl") channelName = "bbc_parliament";

    else if (argStrings[i] == "r1") { channelName = "bbc_radio_one"; radio = true; vidBitrate = 0; }
    else if (argStrings[i] == "r2") { channelName = "bbc_radio_two"; radio = true; vidBitrate = 0; }
    else if (argStrings[i] == "r3") { channelName = "bbc_radio_three"; radio = true; vidBitrate = 0; }
    else if (argStrings[i] == "r4") { channelName = "bbc_radio_fourfm"; radio = true; vidBitrate = 0; }
    else if (argStrings[i] == "r5") { channelName = "bbc_radio_five_live"; radio = true; vidBitrate = 0; }
    else if (argStrings[i] == "r6") { channelName = "bbc_6music"; radio = true; vidBitrate = 0; }

    else if (argStrings[i] == "v0") vidBitrate = 0;
    else if (argStrings[i] == "v1") vidBitrate = 827008;
    else if (argStrings[i] == "v2") vidBitrate = 1604032;
    else if (argStrings[i] == "v3") vidBitrate = 2812032;
    else if (argStrings[i] == "v4") vidBitrate = 5070016;

    else if (argStrings[i] == "48k") audBitrate = 48000;
    else if (argStrings[i] == "96k") audBitrate = 96000;
    else if (argStrings[i] == "128k") audBitrate = 128000;
    else if (argStrings[i] == "320k") audBitrate = 320000;
    }
    //}}}

  cLog::init (logLevel);
  cLog::log (LOGNOTICE, "openGL hls " + channelName  + " " + dec (audBitrate) + " " + dec (vidBitrate));

  eLoaderFlags loaderFlags = eLoaderFlags(eQueueAudio | eQueueVideo);
  if (forceFFmpeg)
    loaderFlags = eLoaderFlags(loaderFlags | eFFmpeg);

  cAppWindow appWindow;
  appWindow.run ("hls", 800, 450, headless, loaderFlags, radio, channelName, audBitrate, vidBitrate);

  return 0;
  }
