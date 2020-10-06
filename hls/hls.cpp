// main.cpp - hls audio/video windows/linux
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

// c++
#include <cstdint>
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
#else
  #include "../../shared/audio/cLinuxAudio16.h"
#endif

// net
#ifdef _WIN32
  #include "../../shared/net/cWinSockHttp.h"
#else
  #include "../../shared/net/cLinuxHttp.h"
#endif

// video
#include "../../shared/utils/cVideoDecode.h"

// widgets
#include "../../shared/nanoVg/cGlWindow.h"
#include "../../shared/fonts/FreeSansBold.h"
#include "../../shared/fonts/DroidSansMono1.h"
#include "../../shared/widgets/cTextBox.h"
#include "../../shared/widgets/cSongWidget.h"
#include "../../shared/widgets/cVideoDecodeWidget.h"

using namespace std;
using namespace chrono;
//}}}
//{{{  channels
constexpr int kAudBitrate = 128000; // 96000  128000

const string kHost = "vs-hls-uk-live.akamaized.net";

const vector <string> kChannels = { "bbc_one_hd",          "bbc_two_hd",          "bbc_four_hd", // pa4
                                    "bbc_news_channel_hd", "bbc_one_scotland_hd", "s4cpbs",      // pa4
                                    "bbc_one_south_west",  "bbc_parliament" };                   // pa3

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
      initialise (title, width, height, (unsigned char*)droidSansMono, sizeof(droidSansMono));
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
          mSong.getSelect().clearAll(); changed();
          break;
        //}}}
        //{{{
        case GLFW_KEY_HOME:      // skip beginning
         mSong.setPlayFrame (mSong.getSelect().empty() ? mSong.getFirstFrame() : mSong.getSelect().getFirstFrame());
          if (mVideoDecode) {
            auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
            if (framePtr)
              mVideoDecode->setPlayPts (framePtr->getPts());
            mVideoDecode->clear (framePtr->getPts());
            }
         //changed();
         break;
        //}}}
        //{{{
        case GLFW_KEY_END:       // skip end
          mSong.setPlayFrame (mSong.getSelect().empty() ? mSong.getLastFrame() : mSong.getSelect().getLastFrame());
          if (mVideoDecode) {
            auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
            if (framePtr)
              mVideoDecode->setPlayPts (framePtr->getPts());
            mVideoDecode->clear (framePtr->getPts());
            }
          //changed();
          break;
        //}}}
        //{{{
        case GLFW_KEY_LEFT:      // skip back
          mSong.incPlaySec (-(mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
          if (mVideoDecode) {
            auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
            if (framePtr)
              mVideoDecode->setPlayPts (framePtr->getPts());
            mVideoDecode->clear (framePtr->getPts());
            }
          //changed();
          break;
        //}}}
        //{{{
        case GLFW_KEY_RIGHT:     // skip forward
          mSong.incPlaySec ((mods == GLFW_MOD_SHIFT ? 300 : mods == GLFW_MOD_CONTROL ? 10 : 1), false);
          if (mVideoDecode) {
            auto framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
            if (framePtr)
              mVideoDecode->setPlayPts (framePtr->getPts());
            mVideoDecode->clear (framePtr->getPts());
            }
          //changed();
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
        case GLFW_KEY_PAGE_UP:   // nothing
          break;
        //}}}
        //{{{
        case GLFW_KEY_PAGE_DOWN: // nothing
          break;
        //}}}

        //{{{
        case GLFW_KEY_M: // mark
          mSong.getSelect().addMark (mSong.getPlayFrame());
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

    constexpr int kPesSize = 1000000;
    uint8_t* audPes = (uint8_t*)malloc (kPesSize);
    uint8_t* vidPes = (uint8_t*)malloc (kPesSize);

    mSong.setChannel (channel);
    mSong.setBitrate (audBitrate, audBitrate < 128000 ? 180 : 360); // audBitrate, audioFrames per chunk

    while (!mExit && !mSongChanged) {
      mSongChanged = false;
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
        while (!mExit && !mSongChanged) {
          constexpr int kHlsPreload = 2;
          int chunkNum = mSong.getHlsLoadChunkNum (system_clock::now(), 12s, kHlsPreload);
          if (chunkNum) {
            // get hls chunkNum chunk
            mSong.setHlsLoad (cSong::eHlsLoading, chunkNum);
            if (http.get (redirectedHost, path + '-' + dec(chunkNum) + ".ts", "",
                          [&] (const string& key, const string& value) noexcept { /* headerCallback lambda */ },
                          [&] (const uint8_t* data, int length) noexcept {
                            // data callback lambda
                            if (mVideoDecode)
                              mVideoDecode->setDecodeFrac (float(http.getContentSize()) / http.getHeaderContentSize());
                            return true; }
                          ) == 200) {
              // got content chunk
              mSong.setHlsLoad (cSong::eHlsIdle, chunkNum);
              int seqFrameNum = mSong.getHlsFrameFromChunkNum (chunkNum);
              if (mVideoDecode)
                mVideoDecode->setDecodeFrac (1.f);
              //{{{  parse content
              int audPesSize = 0;
              uint64_t audPesPts = 0;

              int vidPesNum = 0;
              int vidPesSize = 0;
              uint64_t vidPesPts = 0;

              // parse ts packets
              uint8_t* ts = http.getContent();
              int tsSize = http.getContentSize();
              while ((ts < ts + tsSize) && (*ts++ == 0x47)) {
                auto payStart = ts[0] & 0x40;
                auto pid = ((ts[0] & 0x1F) << 8) | ts[1];
                auto headerSize = (ts[2] & 0x20) ? 4 + ts[3] : 3;
                ts += headerSize;
                auto tsBodySize = 187 - headerSize;

                if (pid == 33) {
                  //{{{  video pid
                  if (payStart && !ts[0] && !ts[1] && (ts[2] == 1) && (ts[3] == 0xe0)) {
                    // new pes
                    if (vidPesSize) {
                      // process prev videoPes
                      mVideoDecode->decode (vidPes, vidPesSize, vidPesNum == 0, vidPesPts);
                      vidPesSize = 0;
                      vidPesNum++;
                      }

                    // header
                    if (ts[7] & 0x80)
                      vidPesPts = getPts (ts+9);

                    headerSize = 9 + ts[8];
                    ts += headerSize;
                    tsBodySize -= headerSize;
                    }

                  // copy ts payload into Pes
                  if (vidPesSize + tsBodySize > kPesSize)
                    cLog::log (LOGERROR, "Pes overflowed %d", vidPesSize + tsBodySize);
                  else {
                    memcpy (vidPes + vidPesSize, ts, tsBodySize);
                    vidPesSize += tsBodySize;
                    }
                  }
                  //}}}
                else if (pid == 34) {
                  //{{{  audio pid
                  if (payStart && !ts[0] && !ts[1] && (ts[2] == 1) && (ts[3] == 0xC0)) {
                    // new pes
                    if (audPesSize) {
                      //  process prev audioPes
                      uint8_t* bufferPtr = audPes;
                      while (audioDecode.parseFrame (bufferPtr, audPes + audPesSize)) {
                        float* samples = audioDecode.decodeFrame (seqFrameNum);
                        if (samples) {
                          mSong.setFixups (audioDecode.getNumChannels(), audioDecode.getSampleRate(), audioDecode.getNumSamples());
                          mSong.addAudioFrame (seqFrameNum++, samples, true, mSong.getNumFrames(), nullptr, audPesPts);
                          audPesPts += (audioDecode.getNumSamples() * 90) / 48;
                          changed();
                          if (!player.joinable())
                            player = thread ([=](){ playThread (true); });  // playThread16 playThread32 playThreadWSAPI
                          }
                        bufferPtr += audioDecode.getNextFrameOffset();
                        }
                      audPesSize = 0;
                      }

                    // header
                    if (ts[7] & 0x80)
                      audPesPts = getPts (ts+9);
                    headerSize = 9 + ts[8];
                    ts += headerSize;
                    tsBodySize -= headerSize;
                    }

                  // copy ts payload into Pes
                  if (audPesSize + tsBodySize > kPesSize)
                    cLog::log (LOGERROR, "audPes overflowed %d", audPesSize + tsBodySize);
                  else {
                    memcpy (audPes + audPesSize, ts, tsBodySize);
                    audPesSize += tsBodySize;
                    }
                  }
                  //}}}

                ts += tsBodySize;
                }

              if (audPesSize) {
                //{{{  process last audPes
                uint8_t* bufferPtr = audPes;
                while (audioDecode.parseFrame (bufferPtr, audPes + audPesSize)) {
                  float* samples = audioDecode.decodeFrame (seqFrameNum);
                  if (samples) {
                    mSong.addAudioFrame (seqFrameNum++, samples, true, mSong.getNumFrames(), nullptr, audPesPts);
                    audPesPts += (audioDecode.getNumSamples() * 90) / 48;
                    changed();
                    }
                  bufferPtr += audioDecode.getNextFrameOffset();
                  }
                }
                //}}}
              if (vidPesSize)
                //{{{  process last vidPes
                mVideoDecode->decode (vidPes, vidPesSize, vidPesNum == 0, vidPesPts);
                vidPesNum++;
                //}}}
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

    free (audPes);
    free (vidPes);
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
        while (!mExit && !mSongChanged) {
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

      cSong::cFrame* framePtr;
      cAudio16 audio (2, mSong.getSampleRate());
      cAudioDecode decode (mSong.getFrameType());

      while (!mExit && !mSongChanged) {
        int16_t* playSamples = silence;
          {
          // scoped song mutex
          shared_lock<shared_mutex> lock (mSong.getSharedMutex());
          framePtr = mSong.getAudioFramePtr (mSong.getPlayFrame());
          bool gotSamples = mPlaying && framePtr && framePtr->getSamples();
          if (gotSamples) {
            float* src = framePtr->getSamples();
            int16_t* dst = samples;
            for (int i = 0; i <mSong.getSamplesPerFrame(); i++) {
              *dst++ = int16_t((*src++) * 0x8000);
              *dst++ = int16_t((*src++) * 0x8000);
              }
            playSamples = samples;
            }
          }
        audio.play (2, playSamples, mSong.getSamplesPerFrame(), 1.f);
        //cLog::log (LOGINFO, "audio.play");

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
  bool mExit = false;
  //}}}
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
