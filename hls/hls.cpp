// main.cpp - hls audio/video windows/linux
// aligned alloc prob
//{{{  includes
#ifdef _WIN32
  //{{{  windows headers, defines
  #define _CRT_SECURE_NO_WARNINGS
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  #include <winsock2.h>
  #include <WS2tcpip.h>
  #include <objbase.h>
  //}}}
#endif

// c
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <emmintrin.h>

// c++
#include <string>
#include <thread>
#include <chrono>

// utils
#include "../../shared/date/date.h"
#include "../../shared/utils/utils.h"
#include "../../shared/utils/cLog.h"

// audio
#include "../../shared/decoders/cAudioDecode.h"
#include "../../shared/utils/cSong.h"

// audio out
#ifdef _WIN32
  #include "../../shared/audio/audioWASAPI.h"
  #include "../../shared/audio/cWinAudio16.h"
  #include "../../shared/audio/cWinAudio32.h"
#else
  #include "../../shared/audio/cLinuxAudio16.h"
#endif

// net
#ifdef _WIN32
  #include "../../shared/net/cWinSockHttp.h"
#else
  #include "../../shared/net/cLinuxHttp.h"
#endif

// widgets
#include "../../shared/nanoVg/cGlWindow.h"
#include "../../shared/fonts/FreeSansBold.h"
#include "../../shared/fonts/DroidSansMono1.h"
#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cSongWidget.h"

#include "../../shared/utils/cVideoDecode.h"

using namespace std;
using namespace chrono;
//}}}
constexpr int kDefaultChannelNum = 3;
constexpr int kVidBitrate = 827008; // 827008 1604032 2812032 5070016
//{{{  channels
constexpr int kAudBitrate = 128000; // 96000  128000
const string kHost = "vs-hls-uk-live.akamaized.net";
const vector <string> kChannels = { "bbc_one_hd",          "bbc_two_hd",          "bbc_four_hd", // pa4
                                    "bbc_news_channel_hd", "bbc_one_scotland_hd", "s4cpbs",      // pa4
                                    "bbc_one_south_west",  "bbc_parliament" };                   // pa3

//}}}

//{{{
class cVideoDecodeWidget : public cWidget {
public:
  cVideoDecodeWidget (cVideoDecode* videoDecode, float width, float height)
    : cWidget (COL_BLACK, width, height), mVideoDecode(videoDecode) {}
  virtual ~cVideoDecodeWidget() {}

  void onDraw (iDraw* draw) {
    auto frame = mVideoDecode->findPlayFrame();
    if (frame) {
      auto context = draw->getContext();
      if (frame->getPts() != mPts) {
        // newFrame, update image
        mPts = frame->getPts();
        if (mImage == -1)
          mImage = context->createImageRGBA (frame->getWidth(), frame->getHeight(), 0, (uint8_t*)frame->get32());
        else
          context->updateImage (mImage, (uint8_t*)frame->get32());
        }

      // paint image rect
      context->beginPath();
      context->rect (0, 0, mWidth, mHeight);
      context->fillPaint (context->imagePattern (0, 0, mWidth, mHeight, 0.f, mImage, 1.f));
      context->triangleFill();
      }
    }

private:
  cVideoDecode* mVideoDecode;
  uint64_t mPts = 0;
  int mImage = -1;
  };
//}}}

class cAppWindow : public cGlWindow {
public:
  cAppWindow() {}
  //{{{
  void run (const string& title, int width, int height,
            bool headless, bool logInfo3,
            int channelNum, int audBitrate, int vidBitrate)  {

    mLogInfo3 = logInfo3;
    //mVideoDecode = new cMfxVideoDecode();
    mVideoDecode = new cFFmpegVideoDecode();

    if (headless) {
      thread ([=](){ hlsThread (kHost, kChannels[channelNum], audBitrate, vidBitrate); }).detach();
      while (true)
        this_thread::sleep_for (200ms);
       }

    else {
      initialise (title, width, height, (unsigned char*)droidSansMono);
      addTopLeft (new cVideoDecodeWidget (mVideoDecode, 0,0));
      addTopLeft (new cSongWidget (&mSong, 0,0));

      thread ([=](){ hlsThread (kHost, kChannels[channelNum], audBitrate, vidBitrate); }).detach();

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
        //{{{
        case GLFW_KEY_ESCAPE: // exit
          glfwSetWindowShouldClose (mWindow, GL_TRUE);
          break;
        //}}}

        case GLFW_KEY_1:
        case GLFW_KEY_2:
        case GLFW_KEY_3:
        case GLFW_KEY_4:
        case GLFW_KEY_5:
        case GLFW_KEY_6: break;

        //{{{
        case GLFW_KEY_SPACE :  // pause
          mPlaying = !mPlaying;
          break;
        //}}}
        //{{{
        case GLFW_KEY_DELETE : // delete select
          mSong.getSelect().clearAll(); changed();
          break;
        //}}}

        //{{{
        case GLFW_KEY_HOME:
         mSong.setPlayFrame (mSong.getSelect().empty() ? mSong.getFirstFrame() : mSong.getSelect().getFirstFrame());
          if (mVideoDecode) {
            auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
            if (framePtr)
              mVideoDecode->setPlayPts (framePtr->getPts());
            mVideoDecode->clear();
            }
         //changed();
         break;
        //}}}
        //{{{
        case GLFW_KEY_END:
          mSong.setPlayFrame (mSong.getSelect().empty() ? mSong.getLastFrame() : mSong.getSelect().getLastFrame());
          if (mVideoDecode) {
            auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
            if (framePtr)
              mVideoDecode->setPlayPts (framePtr->getPts());
            mVideoDecode->clear();
            }
          //changed();
          break;
        //}}}
        //{{{
        case GLFW_KEY_LEFT:
          mSong.incPlaySec (-(mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
          if (mVideoDecode) {
            auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
            if (framePtr)
              mVideoDecode->setPlayPts (framePtr->getPts());
            mVideoDecode->clear();
            }
          //changed();
          break;
        //}}}
        //{{{
        case GLFW_KEY_RIGHT:
          mSong.incPlaySec ((mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
          if (mVideoDecode) {
            auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
            if (framePtr)
              mVideoDecode->setPlayPts (framePtr->getPts());
            mVideoDecode->clear();
            }
          //changed();
          break;
        //}}}
        case GLFW_KEY_DOWN:  break;
        case GLFW_KEY_UP:    break;
        case GLFW_KEY_PAGE_UP:   break;
        case GLFW_KEY_PAGE_DOWN: break;

        //{{{
        case GLFW_KEY_M: // mark
          mSong.getSelect().addMark (mSong.getPlayFrame());
          //changed();
          break;
        //}}}

        case GLFW_KEY_V: toggleVsync(); break;
        case GLFW_KEY_P: togglePerf(); break;
        case GLFW_KEY_S: toggleStats(); break;
        case GLFW_KEY_T: toggleTests(); break;

        case GLFW_KEY_I: toggleSolid(); break;
        case GLFW_KEY_A: toggleEdges(); break;
        case GLFW_KEY_Q: fringeWidth (getFringeWidth() - 0.25f); break;
        case GLFW_KEY_W: fringeWidth (getFringeWidth() + 0.25f); break;

        //{{{
        case GLFW_KEY_L: // toggle log level LOGINFO : LOGINFO3
          mLogInfo3 = ! mLogInfo3;
          cLog::setLogLevel (mLogInfo3 ? LOGINFO3 : LOGINFO);
          break;
        //}}}

        default: cLog::log (LOGNOTICE, "Keyboard %x", key); break;
        }
      }
    }
  //}}}

private:
  //{{{
  static uint64_t getPts (const uint8_t* ts) {
  // return 33 bits of pts,dts

    if ((ts[0] & 0x01) && (ts[2] & 0x01) && (ts[4] & 0x01)) {
      // valid marker bits
      uint64_t pts = ts[0] & 0x0E;
      pts = (pts << 7) | ts[1];
      pts = (pts << 8) | (ts[2] & 0xFE);
      pts = (pts << 7) | ts[3];
      pts = (pts << 7) | (ts[4] >> 1);
      return pts;
      }
    else {
      cLog::log (LOGERROR, "getPts marker bits - %02x %02x %02x %02x 0x02", ts[0], ts[1], ts[2], ts[3], ts[4]);
      return 0;
      }
    }
  //}}}
  //{{{
  static string getTagValue (uint8_t* buffer, const char* tag) {

    const char* tagPtr = strstr ((const char*)buffer, tag);
    const char* valuePtr = tagPtr + strlen (tag);
    const char* endPtr = strchr (valuePtr, '\n');

    return string (valuePtr, endPtr - valuePtr);
    }
  //}}}

  //{{{
  void hlsThread (const string& host, const string& channel, int audBitrate, int vidBitrate) {
  // hls http chunk load and decode thread

    cLog::setThreadName ("hls ");

    constexpr int kHlsPreload = 2;

    uint8_t* pesBuffer = nullptr;
    int pesBufferLen = 0;

    mSong.setChannel (channel);
    mSong.setBitrate (audBitrate, audBitrate < 128000 ? 180 : 360); // audBitrate, audioFrames per chunk

    while (true) {
      const string path = "pool_902/live/uk/" + mSong.getChannel() +
                          "/" + mSong.getChannel() +
                          ".isml/" + mSong.getChannel() +
                          (mSong.getBitrate() < 128000 ? "-pa3=" : "-pa4=") + dec(mSong.getBitrate()) +
                          "-video=" + dec(vidBitrate);
      cPlatformHttp http;
      string redirectedHost = http.getRedirect (host, path + ".m3u8");
      if (http.getContent()) {
        //{{{  got .m3u8, parse for mediaSequence, programDateTimePoint
        int mediaSequence = stoi (getTagValue (http.getContent(), "#EXT-X-MEDIA-SEQUENCE:"));

        istringstream inputStream (getTagValue (http.getContent(), "#EXT-X-PROGRAM-DATE-TIME:"));
        system_clock::time_point programDateTimePoint;
        inputStream >> date::parse ("%FT%T", programDateTimePoint);

        http.freeContent();
        //}}}

        mSong.init (cAudioDecode::eAac, 2, 48000, mSong.getBitrate() < 128000 ? 2048 : 1024); // samplesPerFrame
        mSong.setHlsBase (mediaSequence, programDateTimePoint, -37s, (2*60*60) - 30);
        cAudioDecode audioDecode (cAudioDecode::eAac);

        thread player;
        bool firstTime = true;
        bool firstVideoPts = true;
        mSongChanged = false;
        while (true && !mSongChanged) {
          int chunkNum = mSong.getHlsLoadChunkNum (system_clock::now(), 12s, kHlsPreload);
          if (chunkNum) {
            // get hls chunkNum chunk
            mSong.setHlsLoad (cSong::eHlsLoading, chunkNum);
            if (http.get (redirectedHost, path + '-' + dec(chunkNum) + ".ts") == 200) {
              //{{{  process audio first
              int seqFrameNum = mSong.getHlsFrameFromChunkNum (chunkNum);

              // parse ts packets
              uint64_t pts = 0;
              uint8_t* ts = http.getContent();
              uint8_t* tsEnd = ts + http.getContentSize();
              while ((ts < tsEnd) && (*ts++ == 0x47)) {
                auto payStart = ts[0] & 0x40;
                auto pid = ((ts[0] & 0x1F) << 8) | ts[1];
                auto headerBytes = (ts[2] & 0x20) ? 4 + ts[3] : 3;
                ts += headerBytes;
                auto tsBodyBytes = 187 - headerBytes;

                if (pid == 34) {
                  // audio pid
                  if (payStart && !ts[0] && !ts[1] && (ts[2] == 1) && (ts[3] == 0xC0)) {
                    if (pesBufferLen) {
                      //  process prev audioPes
                      uint8_t* pesBufferPtr = pesBuffer;
                      uint8_t* pesBufferEnd = pesBuffer + pesBufferLen;
                      while (audioDecode.parseFrame (pesBufferPtr, pesBufferEnd)) {
                        // several aacFrames per audio pes
                        float* samples = audioDecode.decodeFrame (seqFrameNum);
                        if (samples) {
                          if (firstTime)
                            mSong.setFixups (audioDecode.getNumChannels(), audioDecode.getSampleRate(), audioDecode.getNumSamples());
                          mSong.addAudioFrame (seqFrameNum++, samples, true, mSong.getNumFrames(), nullptr, pts);
                          pts += (audioDecode.getNumSamples() * 90) / 48;
                          changed();
                          if (firstTime) {
                            firstTime = false;
                            player = thread ([=](){ playThread (true); });  // playThread16 playThread32 playThreadWSAPI
                            }
                          }
                        pesBufferPtr += audioDecode.getNextFrameOffset();
                        }
                      pesBufferLen = 0;
                      }

                    if (ts[7] & 0x80)
                      pts = getPts (ts+9);

                    // skip header
                    int pesHeaderBytes = 9 + ts[8];
                    ts += pesHeaderBytes;
                    tsBodyBytes -= pesHeaderBytes;
                    }

                  // copy ts payload into pesBuffer
                  pesBuffer = (uint8_t*)realloc (pesBuffer, pesBufferLen + tsBodyBytes);
                  memcpy (pesBuffer + pesBufferLen, ts, tsBodyBytes);
                  pesBufferLen += tsBodyBytes;
                  }

                ts += tsBodyBytes;
                }

              if (pesBufferLen) {
                //{{{  process last audioPes
                uint8_t* pesBufferPtr = pesBuffer;
                uint8_t* pesBufferEnd = pesBuffer + pesBufferLen;

                while (audioDecode.parseFrame (pesBufferPtr, pesBufferEnd)) {
                  // several aacFrames per audio pes
                  float* samples = audioDecode.decodeFrame (seqFrameNum);
                  if (samples) {
                    mSong.addAudioFrame (seqFrameNum++, samples, true, mSong.getNumFrames(), nullptr, pts);
                    pts += (audioDecode.getNumSamples() * 90) / 48;
                    changed();
                    }

                  pesBufferPtr += audioDecode.getNextFrameOffset();
                  }

                pesBufferLen = 0;
                }
                //}}}
              //}}}
              mSong.setHlsLoad (cSong::eHlsIdle, chunkNum);
              //{{{  process video second, may block waiting for free videoFrames
              // parse ts packets
              ts = http.getContent();
              while ((ts < tsEnd) && (*ts++ == 0x47)) {
                auto payStart = ts[0] & 0x40;
                auto pid = ((ts[0] & 0x1F) << 8) | ts[1];
                auto headerBytes = (ts[2] & 0x20) ? 4 + ts[3] : 3;
                ts += headerBytes;
                auto tsBodyBytes = 187 - headerBytes;

                if (pid == 33) {
                  //  video pid
                  if (payStart && !ts[0] && !ts[1] && (ts[2] == 1) && (ts[3] == 0xe0)) {
                    if (pesBufferLen) {
                      // process prev videoPes
                      mVideoDecode->decode (firstVideoPts, pts, pesBuffer, pesBufferLen);
                      firstVideoPts = false;
                      pesBufferLen = 0;
                      }

                    if (ts[7] & 0x80)
                      pts = getPts (ts+9);

                    // skip header
                    int pesHeaderBytes = 9 + ts[8];
                    ts += pesHeaderBytes;
                    tsBodyBytes -= pesHeaderBytes;
                    }

                  // copy ts payload into pesBuffer
                  pesBuffer = (uint8_t*)realloc (pesBuffer, pesBufferLen + tsBodyBytes);
                  memcpy (pesBuffer + pesBufferLen, ts, tsBodyBytes);
                  pesBufferLen += tsBodyBytes;
                  }

                ts += tsBodyBytes;
                }

              if (pesBufferLen) {
                // process last videoPes
                mVideoDecode->decode (firstVideoPts, pts, pesBuffer, pesBufferLen);
                pesBufferLen = 0;
                }
              //}}}
              http.freeContent();
              }
            else {
              //{{{  failed to load expected available chunk, back off for 250ms
              mSong.setHlsLoad (cSong::eHlsFailed, chunkNum);
              changed();

              cLog::log (LOGERROR, "late " + dec(chunkNum));
              this_thread::sleep_for (250ms);
              }
              //}}}
            }
          else // no chunk available, back off for 100ms
            this_thread::sleep_for (100ms);
          }
        player.join();
        }
      }

    free (pesBuffer);
    cLog::log (LOGINFO, "exit");
    }
  //}}}

  #ifdef _WIN32
    //{{{
    void playThread (bool streaming) {
    // WSAPI player thread, video just follows play pts

      cLog::setThreadName ("play");
      SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

      float silence [2048*2] = { 0.f };
      float samples [2048*2] = { 0.f };

      auto device = getDefaultAudioOutputDevice();
      if (device) {
        cLog::log (LOGINFO, "device @ %d", mSong.getSampleRate());
        device->setSampleRate (mSong.getSampleRate());
        cAudioDecode decode (mSong.getFrameType());

        device->start();
        while (true && !mSongChanged) {
          device->process ([&](float*& srcSamples, int& numSrcSamples) mutable noexcept {
            // lambda callback - load srcSamples
            shared_lock<shared_mutex> lock (mSong.getSharedMutex());

            auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
            if (mPlaying && framePtr && framePtr->getSamples()) {
              if (mSong.getNumChannels() == 1) {
                //{{{  mono to stereo
                auto src = framePtr->getSamples();
                auto dst = samples;
                for (int i = 0; i < mSong.getSamplesPerFrame(); i++) {
                  *dst++ = *src;
                  *dst++ = *src++;
                  }
                }
                //}}}
              else
                memcpy (samples, framePtr->getSamples(), mSong.getSamplesPerFrame() * mSong.getNumChannels() * sizeof(float));
              srcSamples = samples;
              }
            else
              srcSamples = silence;
            numSrcSamples = mSong.getSamplesPerFrame();

            if (mPlaying && framePtr) {
              if (mVideoDecode)
                mVideoDecode->setPlayPts (framePtr->getPts());
              mSong.incPlayFrame (1, true);
              changed();
              }
            });

          if (!streaming && (mSong.getPlayFrame() > mSong.getLastFrame()))
            break;
          }

        device->stop();
        }

      cLog::log (LOGINFO, "exit");
      }
    //}}}
  #else
    //{{{
    void playThread (bool streaming) {
    // audio16 player thread, video just follows play pts

      cLog::setThreadName ("play");
      //SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

      int16_t samples [2048*2] = { 0 };
      int16_t silence [2048*2] = { 0 };

      cAudio16 audio (2, mSong.getSampleRate());
      cAudioDecode decode (mSong.getFrameType());

      shared_mutex lock;
      while (true && !mSongChanged) {
        lock.lock();
        auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
        if (mPlaying && framePtr && framePtr->getSamples()) {
          float* src = framePtr->getSamples();
          int16_t* dst = samples;
          for (int i = 0; i <mSong.getSamplesPerFrame(); i++) {
            *dst++ = int16_t((*src++) * 0x8000);
            *dst++ = int16_t((*src++) * 0x8000);
            }
          lock.unlock();
          audio.play (2, samples, mSong.getSamplesPerFrame(), 1.f);
          }
        else {
          lock.unlock();
          audio.play (2, silence, mSong.getSamplesPerFrame(), 1.f);
          }

        if (mPlaying && framePtr) {
          if (mVideoDecode)
            mVideoDecode->setPlayPts (framePtr->getPts());
          mSong.incPlayFrame (1, true);
          changed();
          }

        if (!streaming && (mSong.getPlayFrame() > mSong.getLastFrame()))
          break;
        }

      cLog::log (LOGINFO, "exit");
      }
    //}}}
  #endif
  //{{{  vars
  cSong mSong;
  bool mSongChanged = false;
  bool mPlaying = true;
  cVideoDecode* mVideoDecode = nullptr;
  bool mLogInfo3 = false;
  //}}}
  };

int main (int numArgs, char* args[]) {

  vector <string> argStrings;
  for (int i = 1; i < numArgs; i++)
    argStrings.push_back (args[i]);

  bool logInfo3 = false;
  bool headless = false;
  for (size_t i = 0; i < argStrings.size(); i++) {
    if (argStrings[i] == "h") headless = true;
    else if (argStrings[i] == "l") logInfo3 = true;
    }

  cLog::init (logInfo3 ? LOGINFO3 : LOGINFO);
  cLog::log (LOGNOTICE, "hls " + logInfo3 ? "logInfo3 " : "" + headless ? "headless " : "");

  cAppWindow appWindow;
  appWindow.run ("hls", 790, 450, headless, logInfo3, kDefaultChannelNum, kAudBitrate, kVidBitrate);

  return 0;
  }