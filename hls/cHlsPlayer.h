// cHlsPlayer.h
#pragma once
//{{{  includes
// c++
#include <cstdint>
#include <string>
//}}}

class cSong;
class cVideoDecode;
class cHlsPlayer {
public:
  cHlsPlayer() {}
  virtual ~cHlsPlayer();

  void init (const std::string& name);

protected:
  void videoFollowAudio();
  void hlsThread (const std::string& host, const std::string& channel, int audBitrate, int vidBitrate);

  void playThread (bool streaming);

  // vars
  bool mExit = false;
  cSong* mSong;
  bool mSongChanged = false;
  bool mPlaying = true;
  cVideoDecode* mVideoDecode = nullptr;
  };
