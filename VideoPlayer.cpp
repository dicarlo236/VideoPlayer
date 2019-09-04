#include "VideoPlayer.h"
#include <stdio.h>
#include <assert.h>

static const char* modeNames[] = {
  "PLAY",
  "REWIND",
  "PAUSE",
  "FF",
  "FB"
};

/*!
 * Get a string representing the name of a playback mode
 */
static const char* getModeName(PlaybackMode mode) {
  return modeNames[(int)mode];
}

/*!
 * construct a new video player
 * @param fileName : name of file to open
 * @param maxMemory : maximum memory to be used by cache
 */
VideoPlayer::VideoPlayer(const std::string &fileName, uint64_t maxMemory) :_name(fileName) , _cache(maxMemory){
  setup();
}

/*!
 * Add a frame to the cache.
 * @param data : pointer to frame data
 * @param size : size of data
 * @param frame : frame number
 */
void VideoCache::addFrame(uint8_t *data, uint64_t size, int frame) {
  // don't cache frames we already have
  if(_frameMap.find(frame) != _frameMap.end()) return;

  // track memory usage of cache
  _totalMemUse += sizeof(FrameRecord) + size;

  // build new record
  auto* rec = new FrameRecord;
  rec->data = new uint8_t[size];
  rec->frame = frame;
  rec->use_id = _useCount++;
  rec->size = size;
  //next, prev.

  // copy the data into the cache
  memcpy(rec->data, data, size);

  // add to map
  _frameMap[frame] = rec;
  assert(_frameMap.find(frame) != _frameMap.end());

  // make sure we aren't over the memory budget
  while(_totalMemUse > 1024l * 1024l * _maxMemory) {
    // clean!
    cleanFrame();
  }

}

/*!
 * Remove the most likely to be useless frame (this heuristic is currently bad, and means I have to
 * add a workaround to make playback smooth, though I don't quite understand why).
 */
void VideoCache::cleanFrame() {
  // find oldest frame in cache
  int frame = -1;
  uint64_t minAge = UINT64_MAX;
  for(auto& rec : _frameMap) {
    if(rec.second->use_id < minAge) {
      frame = rec.second->frame;
      minAge = rec.second->use_id;
    }
  }

  // and kill it
  if(frame != -1) {
    auto& rec = _frameMap[frame];
    _totalMemUse -= (sizeof(FrameRecord) + rec->size);
    delete[] rec->data;
    delete rec;
    _frameMap.erase(frame);
  }
}

/*!
 * get a frame from the cache and update is age. null if it isn't in the cache
 */
FrameRecord* VideoCache::getFrame(int frame) {
  auto kv = _frameMap.find(frame);
  if(kv != _frameMap.end()) {
    kv->second->use_id = _useCount++;
    return kv->second;
  }
  return nullptr;
}


/*!
 * Setup video player
 */
void VideoPlayer::setup() {

  // setup libs
  av_register_all();
  avformat_network_init(); // todo remove me?
  _context = avformat_alloc_context();

  // open and set up codec
  if(avformat_open_input(&_context, _name.c_str(), nullptr, nullptr)) {
    printf("failed to open file\n");
    return;
  }

  if(avformat_find_stream_info(_context, nullptr) < 0) {
    printf("Invalid file\n");
    return;
  }

  for(int i = 0; i < _context->nb_streams; i++) {
    if(_context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      _videoStreamIdx = i;
      break;
    }
  }

  if(_videoStreamIdx == -1) {
    printf("Failed to find video data\n");
    return;
  }

  printf("Stream %d contains video data\n", _videoStreamIdx);

  _codecContext = _context->streams[_videoStreamIdx]->codec;
  _codec = avcodec_find_decoder(_codecContext->codec_id);

  if(!_codec) {
    printf("Failed to find codec\n");
    return;
  }

  if(avcodec_open2(_codecContext, _codec, nullptr) < 0) {
    printf("failed to open codec\n");
    return;
  }

  // allocate output frame
  _frame = av_frame_alloc();
  _frameYUV = av_frame_alloc();

  printf("codec w: %d h: %d\n", _codecContext->width, _codecContext->height);

  _frameSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
                                        _codecContext->width, _codecContext->height, 1);

  printf("frame size: %ld bytes (%.3f MB)\n", _frameSize, _frameSize / (1024. * 1024.));


  _frameData = (uint8_t*)av_malloc(_frameSize);

  av_image_fill_arrays(_frameYUV->data, _frameYUV->linesize, _frameData, AV_PIX_FMT_YUV420P, _codecContext->width, _codecContext->height, 1);


  printf("info-------\n");
  av_dump_format(_context, 0, _name.c_str(), 0);
  printf("-----------\n\n\n");


  // SDL setup

  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    printf("SDL init error: %s\n", SDL_GetError());
    return;
  }
  TTF_Init();

  _window = SDL_CreateWindow("Video Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             _codecContext->width, _codecContext->height, SDL_WINDOW_OPENGL);

  if(!_window) {
    printf("SDL window error: %s\n", SDL_GetError());
    return;
  }

  _renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  _texture = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, _codecContext->width, _codecContext->height);

  _convert = sws_getContext(_codecContext->width, _codecContext->height, _codecContext->pix_fmt,
                            _codecContext->width, _codecContext->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);

  _timeBase = (int64_t(_codecContext->time_base.num) * AV_TIME_BASE) / int64_t(_codecContext->time_base.den);

  _font = TTF_OpenFont("../font.ttf", 24);

  _frameDataSize = _codecContext->width *  _codecContext->height * 12 / 8;

}

int64_t VideoPlayer::ptsToFrame(int64_t pts) {
  return (pts - _ptsZero) / _seekTimeBase;
}

int64_t VideoPlayer::frameToPts(int frame) {
  return _ptsZero + (frame * _seekTimeBase);
}

void VideoPlayer::exitIfNeeded() {
  SDL_Event event;

  while(SDL_PollEvent(&event)) {
    switch(event.type) {
      case SDL_QUIT:
        exit(0);
        break;
      case SDL_KEYDOWN:
      {
        switch(event.key.keysym.sym) {
          case SDLK_l:
            _mode = PLAY;
            break;
          case SDLK_k:
            _mode = PAUSE;
            break;
          case SDLK_j:
            _mode = REWIND;
            break;
          case SDLK_f:
            _mode = FRAME_FORWARD;
            break;
          case SDLK_d:
            _mode = FRAME_BACKWARD;
            break;
          case SDLK_c:
            _cacheDebug = !_cacheDebug;
            break;
        }
      }
        break;
    }
  }
}

int VideoPlayer::determineNextFrame() {
  switch(_mode) {
    case PLAY:
      return _frameDisplayed + 1;

    case REWIND:
      return _frameDisplayed - 1;

    case PAUSE:
      return _frameDisplayed;

    case FRAME_FORWARD:
      _mode = PAUSE;
      return _frameDisplayed + 1;

    case FRAME_BACKWARD:
      _mode = PAUSE;
      return _frameDisplayed - 1;
  }

  return 0;
}

void VideoPlayer::displayConsecutive() {
  AVPacket packet;
  int readyToDisplay = 0;

  while(!readyToDisplay) { // until got picture

    //avcodec_flush_buffers(_codecContext);
    //av_seek_frame(_context, -1, int64_t(_frameCount) * _timeBase, AVSEEK_FLAG_BACKWARD);

    bool gotVideoPacket = false;
    while(!gotVideoPacket) { // until got video stream
      if(av_read_frame(_context, &packet) < 0) {
        printf("couldn't read frame!\n");
        exit(0);
      }
      if(packet.stream_index == _videoStreamIdx) gotVideoPacket = true;
    }

    if (avcodec_decode_video2(_codecContext, _frame, &readyToDisplay, &packet) < 0) {
      printf("decode error\n");
      return;
    }
  }



  if(!_ptsZeroSet) {
    _ptsZero = packet.pts;
    _seekTimeBase = packet.duration;
    _ptsZeroSet = true;
  }

  _currentDecoderFrame = ptsToFrame(packet.pts);

  updateCacheIfNeeded(_currentDecoderFrame, _frame->data[0]);


//  if(packet.flags && AV_PKT_FLAG_KEY) printf("[KEY] "); else printf("      ");
//  printf("CONSECUTIVE: f %d, f_des %d", _currentDecoderFrame, _desiredNextFrame);
//  if(packet.flags && AV_PKT_FLAG_KEY)
//    printf(" @ %ld\n", packet.pts);
//  else printf("\n");

  if(packet.flags && AV_PKT_FLAG_KEY) {
    printf("[KEY] ");
    printf("CONSECUTIVE: f %d, f_des %d", _currentDecoderFrame, _desiredNextFrame);
    if(packet.flags && AV_PKT_FLAG_KEY)
      printf(" @ %ld\n", packet.pts);
    else printf("\n");
  }

}

void VideoPlayer::displaySeekBackward() {
  AVPacket packet;
  printf("SEEK: %d\n", _desiredNextFrame);

  int seekTarget = _desiredNextFrame;   // where we try to seek to
  int lastSeekTarget = _currentDecoderFrame;   // last place we tried to seek to
  int seekResult = _desiredNextFrame + 1; // the result of our most recent seek
  bool seekZero = false; // if we've tried seeking to zero yet

  while(seekResult > _desiredNextFrame) { // we want to get a frame before/equal to the desired

    if(seekTarget < 0) {
      seekTarget = 0;
      if(seekZero) {
        printf("warning seek 0 when backward seeking\n");
        exit(0);
      }
      seekZero = true;
    }

    // seek to target
    // printf("  target: %d (last %d)\n", seekTarget, lastSeekTarget);
    avcodec_flush_buffers(_codecContext);
    av_seek_frame(_context, _videoStreamIdx, frameToPts(seekTarget), lastSeekTarget > seekTarget ? AVSEEK_FLAG_BACKWARD : 0);
    avcodec_flush_buffers(_codecContext);

    // get packet
    int readyToDisplay = 0;
    while(!readyToDisplay) { // until got picture

      bool gotVideoPacket = false;
      while(!gotVideoPacket) { // until got video stream
        if(av_read_frame(_context, &packet) < 0) {
          //printf("couldn't read frame!\n");
          continue;
        }
        if(packet.stream_index == _videoStreamIdx) gotVideoPacket = true;
      }

      if (avcodec_decode_video2(_codecContext, _frame, &readyToDisplay, &packet) < 0) {
        printf("decode error\n");
        return;
      }
    }


    seekResult = ptsToFrame(packet.pts);
    updateCacheIfNeeded(seekResult, _frame->data[0]);
    // printf("  result: %d\n", seekResult);


    lastSeekTarget = seekTarget;
    seekTarget -= 30;
  }

  // seek forward
  while(seekResult < _desiredNextFrame) {
    int readyToDisplay = 0;
    while(!readyToDisplay) { // until got picture

      bool gotVideoPacket = false;
      while(!gotVideoPacket) { // until got video stream
        if(av_read_frame(_context, &packet) < 0) {
          //printf("couldn't read frame!\n");
          continue;
        }
        if(packet.stream_index == _videoStreamIdx) gotVideoPacket = true;
      }

      if (avcodec_decode_video2(_codecContext, _frame, &readyToDisplay, &packet) < 0) {
        printf("decode error\n");
        return;
      }
    }
    seekResult = ptsToFrame(packet.pts);
    updateCacheIfNeeded(seekResult, _frame->data[0]);
  }


  _currentDecoderFrame = ptsToFrame(packet.pts);
  // printf("BWD: f %d, f_des %d\n", _currentDecoderFrame, _desiredNextFrame);
}

void VideoPlayer::displaySeekForward() {
  AVPacket packet;

  // printf("SEEK: %d\n", _desiredNextFrame);

  int seekTarget = _desiredNextFrame;
  int lastSeekTarget = _currentDecoderFrame;
  int seekResult = _desiredNextFrame + 1;

  bool seekZero = false;

  // seek to keyframe before desired frame
  while(seekResult > _desiredNextFrame) {
    if(seekTarget < 0) {
      seekTarget = 0;
      if(seekZero) {
        printf("warning seek 0\n");
        exit(0);
      }
      seekZero = true;
    }

    // seek to target
    // printf("  target: %d (last %d)\n", seekTarget, lastSeekTarget);
    avcodec_flush_buffers(_codecContext);
    av_seek_frame(_context, _videoStreamIdx, frameToPts(seekTarget), lastSeekTarget > seekTarget ? AVSEEK_FLAG_BACKWARD : 0);
    avcodec_flush_buffers(_codecContext);


    // get packet
    int readyToDisplay = 0;
    while(!readyToDisplay) { // until got picture

      bool gotVideoPacket = false;
      while(!gotVideoPacket) { // until got video stream
        if(av_read_frame(_context, &packet) < 0) {
          //printf("couldn't read frame!\n");
          continue;
        }
        if(packet.stream_index == _videoStreamIdx) gotVideoPacket = true;
      }

      if (avcodec_decode_video2(_codecContext, _frame, &readyToDisplay, &packet) < 0) {
        printf("decode error\n");
        return;
      }
    }


    seekResult = ptsToFrame(packet.pts);
    //updateCacheIfNeeded(seekResult, _frame->data[0]);
    // printf("  result: %d\n", seekResult);


    lastSeekTarget = seekTarget;
    seekTarget -= 30;
  }

  // seek forward
  while(seekResult < _desiredNextFrame) {
    int readyToDisplay = 0;
    while(!readyToDisplay) { // until got picture

      bool gotVideoPacket = false;
      while(!gotVideoPacket) { // until got video stream
        if(av_read_frame(_context, &packet) < 0) {
          //printf("couldn't read frame!\n");
          continue;
        }
        if(packet.stream_index == _videoStreamIdx) gotVideoPacket = true;
      }

      if (avcodec_decode_video2(_codecContext, _frame, &readyToDisplay, &packet) < 0) {
        printf("decode error\n");
        return;
      }
    }
    seekResult = ptsToFrame(packet.pts);
    updateCacheIfNeeded(seekResult, _frame->data[0]);
  }


  _currentDecoderFrame = ptsToFrame(packet.pts);
  // printf("FWD: f %d, f_des %d\n", _currentDecoderFrame, _desiredNextFrame);
}

void VideoPlayer::seekTo(int frame) {
  if(frame == _currentDecoderFrame + 1) {
    displayConsecutive();
  } else if(frame > _currentDecoderFrame) {
    displaySeekForward();
  } else {
    displaySeekBackward();
  }
}

void VideoPlayer::playback() {

  exitIfNeeded();
  _desiredNextFrame = determineNextFrame();
  // printf("next %d\n", _desiredNextFrame);

  bool usedCache = true;
  if(!tryCache(_desiredNextFrame)) {
    seekTo(_desiredNextFrame);
    usedCache = false;
    _frameDisplayed = _currentDecoderFrame;
  }


  if(_desiredNextFrame != _frameDisplayed) {
    printf("[ERROR] wanted frame %d, got %d instead!\n", _desiredNextFrame, _currentDecoderFrame);
  }

  SDL_Color fontColor = {255, 255, 255};
  char status_bar[1024];
  sprintf(status_bar, "f %05d, c %07.2f MB, t %02d:%02d, m %s %c", _frameDisplayed, _cache.getMB(), _frameDisplayed/60, _frameDisplayed%60, getModeName(_mode), usedCache ? 'C':' ');
  SDL_Surface* fontSurface = TTF_RenderText_Solid(_font, status_bar, fontColor);
  SDL_Texture* fontTexture = SDL_CreateTextureFromSurface(_renderer, fontSurface);
  int texW = 0;
  int texH = 0;
  SDL_QueryTexture(fontTexture, NULL, NULL, &texW, &texH);
  SDL_Rect fontRect = { 0, 0, texW, texH };

  if(!usedCache)
    sws_scale(_convert, (const unsigned char* const*)_frame->data, _frame->linesize, 0, _codecContext->height, _frameYUV->data, _frameYUV->linesize);
  SDL_UpdateTexture(_texture, nullptr, _frameYUV->data[0], _frameYUV->linesize[0]);
  SDL_RenderClear(_renderer);
  SDL_RenderCopy(_renderer, _texture, nullptr, nullptr);
  SDL_RenderFillRect(_renderer, &fontRect);
  SDL_RenderCopy(_renderer, fontTexture, nullptr, &fontRect);
  if(_cacheDebug)
    debugDrawCache();
  SDL_RenderPresent(_renderer);

  SDL_FreeSurface(fontSurface);
  SDL_DestroyTexture(fontTexture);
}

void VideoPlayer::debugDrawCache() {
  int thickness = 50;
  SDL_Rect rect = {0,thickness,1920,thickness};

  SDL_SetRenderDrawColor(_renderer,255,0,0,255);
  SDL_RenderFillRect(_renderer, &rect);
  SDL_SetRenderDrawColor(_renderer,0,255,0,255);

  for(auto& kv : _cache._frameMap) {
    int x = kv.second->frame;
    if(x >= 1920) continue;
    int y0 = thickness;
    int y1 = y0 + thickness;
    SDL_RenderDrawLine(_renderer,x,y0,x,y1);
  }

  SDL_SetRenderDrawColor(_renderer,0,0,255,255);
  int x = _frameDisplayed;
  if(x < 1920) {
    int y0 = thickness;
    int y1 = y0 + thickness;
    SDL_RenderDrawLine(_renderer,x,y0,x,y1);
  }



  SDL_SetRenderDrawColor(_renderer,0,0,0,255);
}

void VideoPlayer::updateCacheIfNeeded(int frame, uint8_t *data) {
  sws_scale(_convert, (const unsigned char* const*)_frame->data, _frame->linesize, 0, _codecContext->height, _frameYUV->data, _frameYUV->linesize);
  _cache.addFrame(_frameYUV->data[0], _frameDataSize, frame);
}

bool VideoPlayer::tryCache(int frame) {

  auto* result = _cache.getFrame(frame);
//  if(_mode == PLAY && !_cache.getFrame(frame + 1)) {
//    return false;
//  }
  if(result) {
    _frameDisplayed = frame;

    // printf("got cache %d\n", frame);
    memcpy(_frameYUV->data[0], result->data, result->size);
    //memset(_frameYUV->data[0], 0, result->size);
    return true;
  }

  // printf("miss cache %d\n", frame);


  return false;
}