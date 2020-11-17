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
#include <chrono>
#include <thread>

// utils
#include "../../shared/utils/cLog.h"

// loader
#include "../../shared/utils/cLoader.h"

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

static const vector<string> kWqxr  = {"http://stream.wqxr.org/js-stream.aac"};
//}}}

// cAppWindow
class cAppWindow : public cGlWindow {
public:
  //{{{
  void run (const string& title, int width, int height, bool gui, const vector <string>& strings)  {

    if (gui) {
      cGlWindow::initialise (title, width, height, (uint8_t*)droidSansMono, sizeof(droidSansMono));

      // main full screen widget
      mLoaderWidget = new cLoaderWidget (&mLoader, this);
      addTopLeft (mLoaderWidget);

      // add channel icons to mIcons container
      mIcons = new cContainer (0.f, 2.5f, "iconsContainer");
      addTopLeft (mIcons);

      // radio
      constexpr float kIcon = 2.5f * cWidget::kBox;
      mIcons->add (new cImageWidget (r1, sizeof(r1), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio1); }, "r1"));
      mIcons->add (new cImageWidget (r2, sizeof(r2), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio2); }, "r2"));
      mIcons->add (new cImageWidget (r3, sizeof(r3), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio3); }, "r3"));
      mIcons->add (new cImageWidget (r4, sizeof(r4), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio4); }, "r4"));
      mIcons->add (new cImageWidget (r5, sizeof(r5), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio5); }, "r5"));
      mIcons->add (new cImageWidget (r6, sizeof(r6), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kRadio6); }, "r6"));

      // tv
      mIcons->add (new cImageWidget (bbc1, sizeof(bbc1), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kBbc1); } ));
      mIcons->add (new cImageWidget (bbc2, sizeof(bbc2), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kBbc2); } ));
      mIcons->add (new cImageWidget (bbc4, sizeof(bbc4), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kBbc4); } ));
      mIcons->add (new cImageWidget (bbcnews, sizeof(bbcnews),kIcon, kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kNews); } ));
      mIcons->add (new cImageWidget (bbc1, sizeof(bbc1), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kBbcSw); } ));

      mIcons->add (new cImageWidget (bbc1, sizeof(bbc1), kIcon,kIcon, [&](cImageWidget* widget) noexcept {
        mLoader.load (kWqxr); } ));

      // run loader
      mLoader.load (strings);

      // run gui
      cGlWindow::run (false);
      }

    else {
      // run loader
      mLoader.load (strings.empty() ? kRadio4 : strings);

      // no gui, probably don't get the exit keystroke
      while (!mExit)
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
        case GLFW_KEY_1: mLoader.load (kRadio1); break;
        case GLFW_KEY_2: mLoader.load (kRadio2); break;
        case GLFW_KEY_3: mLoader.load (kRadio3); break;
        case GLFW_KEY_4: mLoader.load (kRadio4); break;
        case GLFW_KEY_5: mLoader.load (kRadio5); break;
        case GLFW_KEY_6: mLoader.load (kRadio6); break;

        // !!! return false is key not handled , we could push next icon ???
        case GLFW_KEY_SPACE: mLoader.togglePlaying(); break;
        case GLFW_KEY_HOME:  mLoader.skipBegin(); break;
        case GLFW_KEY_END:   mLoader.skipEnd(); break;
        case GLFW_KEY_LEFT:  mLoader.skipBack (mods == GLFW_MOD_SHIFT, mods == GLFW_MOD_CONTROL); break;
        case GLFW_KEY_RIGHT: mLoader.skipForward (mods == GLFW_MOD_SHIFT, mods == GLFW_MOD_CONTROL); break;

        case GLFW_KEY_M:      mLoader.getSong()->getSelect().addMark (mLoader.getSong()->getPlayPts()); break;
        case GLFW_KEY_DELETE: mLoader.getSong()->getSelect().clearAll(); break;

        case GLFW_KEY_V: toggleVsync(); break;
        case GLFW_KEY_P: togglePerf(); break;
        case GLFW_KEY_S: toggleStats(); break;
        case GLFW_KEY_T: toggleTests(); break;
        case GLFW_KEY_I: toggleSolid(); break;
        case GLFW_KEY_A: toggleEdges(); break;
        case GLFW_KEY_Q: setFringeWidth (getFringeWidth() - 0.25f); break;
        case GLFW_KEY_W: setFringeWidth (getFringeWidth() + 0.25f); break;
        case GLFW_KEY_F: toggleFullScreen(); break;
        //{{{
        case GLFW_KEY_G: // toggle selected graphics
          if (mLoaderWidget)
            mLoaderWidget->toggleGraphics();
          if (mIcons)
            mIcons->toggleVisible();
          break;
        //}}}

        case GLFW_KEY_L: cLog::cycleLogLevel(); break;
        //{{{
        case GLFW_KEY_ESCAPE: // exit
          mExit = true;
          mLoader.exit();
          glfwSetWindowShouldClose (mWindow, GL_TRUE);
          break;
        //}}}

        case GLFW_KEY_DOWN:
        case GLFW_KEY_UP:
        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_PAGE_DOWN:

        default: cLog::log (LOGNOTICE, "Keyboard %x", key); break;
        }
      }
    }
  //}}}
private:
  //{{{  vars
  cLoader mLoader;

  cLoaderWidget* mLoaderWidget = nullptr;
  cContainer* mIcons = nullptr;

  bool mExit = false;
  //}}}
  };

// main
int main (int numArgs, char* args[]) {
  //{{{  args to strings
  vector <string> params;
  for (int i = 1; i < numArgs; i++)
    params.push_back (args[i]);
  //}}}

  // default params
  bool gui = true;
  eLogLevel logLevel = LOGINFO;
  for (auto it = params.begin(); it < params.end(); ++it) {
    //{{{  parse for params
    if (*it == "h") { gui = false; params.erase (it); }
    else if (*it == "l1") { logLevel = LOGINFO1; params.erase (it); }
    else if (*it == "l2") { logLevel = LOGINFO2; params.erase (it); }
    else if (*it == "l3") { logLevel = LOGINFO3; params.erase (it); }
    }
    //}}}

  cLog::init (logLevel);
  cLog::log (LOGNOTICE, "openGL hls");

  cAppWindow appWindow;
  appWindow.run ("hls", 800, 450, gui, params);
  return 0;
  }
