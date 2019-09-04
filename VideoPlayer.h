#ifndef VIDEOPLAYER_VIDEOPLAYER_H
#define VIDEOPLAYER_VIDEOPLAYER_H


#include <string>
#include <unordered_map>

extern "C" {
#include "SDL.h"
#include <SDL2/SDL_ttf.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};



#include <time.h>

class Timer {
public:
  explicit Timer() { start(); }

  void start() { clock_gettime(CLOCK_MONOTONIC, &_startTime); }

  double getMs() { return (double)getNs() / 1.e6; }

  int64_t getNs() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)(now.tv_nsec - _startTime.tv_nsec) +
           1000000000 * (now.tv_sec - _startTime.tv_sec);
  }

  double getSeconds() { return (double)getNs() / 1.e9; }

  struct timespec _startTime;
};

/*!
 * User selectable playback modes
 */
enum PlaybackMode {
  PLAY,    // advance at 60 fps
  REWIND,  // rewind at 60 fps
  PAUSE,   // pause the video
  FRAME_FORWARD, // advance a single frame, then pause
  FRAME_BACKWARD // go back a single frame, then pause
};


/*!
 * A record of a frame which is cached.
 */
struct FrameRecord {
  uint8_t* data;
  int frame;
  uint64_t use_id;
  uint64_t size;
};


/*!
 * A collection of decoded frames
 */
class VideoCache {
public:
  VideoCache(uint64_t maxMemory) : _maxMemory(maxMemory) { }
  void addFrame(uint8_t* data, uint64_t size, int frame);
  void cleanFrame();

  double getMB() {
    return _totalMemUse / (1024. * 1024.);
  }

  FrameRecord* getFrame(int frame);
  std::unordered_map<int, FrameRecord*> _frameMap;
private:

  uint64_t _totalMemUse = 0;
  uint64_t _useCount = 0;
  uint64_t _maxMemory;
};


class VideoPlayer {
public:
  explicit VideoPlayer(const std::string& fileName, uint64_t maxMemory);
  void playback();
private:
  void debugDrawCache();
  void setup();
  void exitIfNeeded();
  int determineNextFrame();
  void seekTo(int frame);
  void displayConsecutive();
  void displaySeekForward();
  void displaySeekBackward();
  void updateCacheIfNeeded(int frame, uint8_t* data);
  bool tryCache(int frame);
  int64_t ptsToFrame(int64_t pts);
  int64_t frameToPts(int frame);

  std::string _name;
  AVFormatContext* _context;
  AVCodecContext* _codecContext;
  AVCodec* _codec;
  AVFrame* _frame, * _frameYUV;
  int _videoStreamIdx = -1;

  uint8_t* _frameData;
  size_t _frameSize;

  SDL_Window* _window;
  SDL_Renderer* _renderer;
  SDL_Texture* _texture;

  SwsContext* _convert;

  int _currentDecoderFrame = 0;
  int _frameDisplayed = 0;
  int _desiredNextFrame = 0;
  bool rewind = false;

  int64_t _timeBase;
  int64_t _seekTimeBase;

  bool _ptsZeroSet = false;
  int64_t _ptsZero;

  VideoCache _cache;
  bool _usedCache = false;
  bool _cacheDebug = false;
  bool _helpOpen = true;
  TTF_Font* _font;
  PlaybackMode _mode = PLAY;
  uint64_t _frameDataSize;

  Timer _frameTimer;
  double _ftAvg = 0;
};



#endif //VIDEOPLAYER_VIDEOPLAYER_H
