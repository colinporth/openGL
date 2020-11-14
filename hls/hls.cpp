// main.cpp - vg openGL - hls,file,icycast audio/video windows/linux
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

// video pool
#include "../../shared/utils/iVideoPool.h"

// loader
#include "../../shared/utils/cLoader.h"
#include "../../shared/utils/cSongPlayer.h"

// widgets
#include "../../shared/vg/cGlWindow.h"
#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cImageWidget.h"
#include "../../shared/widgets/cLoaderWidget.h"
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
//{{{  const
static const vector<string> kRadio1 = {"r1", "a128"};
static const vector<string> kRadio2 = {"r2", "a128"};
static const vector<string> kRadio3 = {"r3", "a320"};
static const vector<string> kRadio4 = {"r4", "a64"};
static const vector<string> kRadio5 = {"r5", "a128"};
static const vector<string> kRadio6 = {"r6", "a128"};

static const vector<string> kBbc1   = {"bbc1", "a128"};
static const vector<string> kBbc2   = {"bbc2", "a128"};
static const vector<string> kBbc4   = {"bbc4", "a128"};
static const vector<string> kNews   = {"news", "a128"};
static const vector<string> kBbcSw  = {"sw", "a128"};
//}}}

// cAppWindow
class cAppWindow : public cGlWindow {
public:
  //{{{
  void run (const string& title, int width, int height, bool gui, const vector <string>& strings)  {

    if (gui) {
      cGlWindow::initialise (title, width, height, (uint8_t*)droidSansMono, sizeof(droidSansMono));

      addTopLeft (new cLoaderWidget (&mLoader, this, cPointF()));

      // add channel icons
      mIcons = new cContainer (0.f, 2.5f);
      addTopLeft (mIcons);

      mIcons->addTopLeft (new cImageWidget(r1, sizeof(r1), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio1); } ));
      mIcons->add (new cImageWidget(r2, sizeof(r2), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio2); } ));
      mIcons->add (new cImageWidget(r3, sizeof(r3), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio3); } ));
      mIcons->add (new cImageWidget(r4, sizeof(r4), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio4); } ));
      mIcons->add (new cImageWidget(r5, sizeof(r5), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio5); } ));
      mIcons->add (new cImageWidget(r6, sizeof(r6), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio6); } ));

      mIcons->add (new cImageWidget(bbc1, sizeof(bbc1), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kBbc1); } ));
      mIcons->add (new cImageWidget(bbc2, sizeof(bbc2), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kBbc2); } ));
      mIcons->add (new cImageWidget(bbc4, sizeof(bbc4), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kBbc4); } ));
      mIcons->add (new cImageWidget(bbcnews, sizeof(bbcnews), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kNews); } ));
      mIcons->add (new cImageWidget(bbc1, sizeof(bbc1), 2.5f,2.5f, [&](cImageWidget* widget) noexcept {
        mLoader.load (kBbcSw); } ));

      mLoader.load (strings);
      cGlWindow::run (false);
      }

    else {
      // no gui
      mLoader.load (strings.empty() ? kRadio4 : strings);
      while (true)
        this_thread::sleep_for (200ms);
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
        //{{{
        case GLFW_KEY_SPACE: // toggle play/pause
          mLoader.getSong()->togglePlaying();
          break;
        //}}}
        //{{{
        case GLFW_KEY_HOME:  // skip beginning
          mLoader.getSong()->setPlayFirstFrame();
          mLoader.skipped();
          break;
        //}}}
        //{{{
        case GLFW_KEY_END:   // skip end
          mLoader.getSong()->setPlayLastFrame();
          mLoader.skipped();
          break;
        //}}}
        //{{{
        case GLFW_KEY_LEFT:  // skip back
          mLoader.getSong()->incPlaySec (-(mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
          mLoader.skipped();
          break;
        //}}}
        //{{{
        case GLFW_KEY_RIGHT: // skip forward
          mLoader.getSong()->incPlaySec ((mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
          mLoader.skipped();
          break;
        //}}}

        //{{{
        case GLFW_KEY_M:      // mark select
          mLoader.getSong()->getSelect().addMark (mLoader.getSong()->getPlayPts());
          //changed();
          break;
        //}}}
        //{{{
        case GLFW_KEY_DELETE: // delete select
          mLoader.getSong()->getSelect().clearAll();
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
        case GLFW_KEY_G: // toggleShowGraphics
          mLoader.toggleShowGraphics();
          if (mIcons)
            mIcons->setVisible (mLoader.getShowGraphics());
          break;
        //}}}
        //{{{
        case GLFW_KEY_F: // toggle fullScreen
          toggleFullScreen();
          break;
        //}}}
        //{{{
        case GLFW_KEY_L: // cycle logLevel
          cLog::cycleLogLevel();
          break;
        //}}}

        //{{{
        case GLFW_KEY_ESCAPE: // exit
          mLoader.stopAndWait();
          glfwSetWindowShouldClose (mWindow, GL_TRUE);
          break;
        //}}}

        case GLFW_KEY_DOWN:
        case GLFW_KEY_UP:
        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_PAGE_DOWN:
        case GLFW_KEY_1:
        case GLFW_KEY_2:
        case GLFW_KEY_3:
        case GLFW_KEY_4:
        case GLFW_KEY_5:
        case GLFW_KEY_6: break;

        default: cLog::log (LOGNOTICE, "Keyboard %x", key); break;
        }
      }
    }
  //}}}
private:
  cLoader mLoader;
  cContainer* mIcons = nullptr;
  };

// main
int main (int numArgs, char* args[]) {
  //{{{  args to strings
  vector <string> strings;
  for (int i = 1; i < numArgs; i++)
    strings.push_back (args[i]);
  //}}}

  // default params
  bool gui = true;
  eLogLevel logLevel = LOGINFO;
  for (auto it = strings.begin(); it < strings.end(); ++it) {
    //{{{  parse for params
    if (*it == "h") { gui = false; strings.erase (it); }
    else if (*it == "l1") { logLevel = LOGINFO1; strings.erase (it); }
    else if (*it == "l2") { logLevel = LOGINFO2; strings.erase (it); }
    else if (*it == "l3") { logLevel = LOGINFO3; strings.erase (it); }
    }
    //}}}

  cLog::init (logLevel);
  cLog::log (LOGNOTICE, "openGL hls");

  cAppWindow appWindow;
  appWindow.run ("hls", 800, 450, gui, strings);
  return 0;
  }
