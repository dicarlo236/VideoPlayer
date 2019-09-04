#include <stdio.h>
#include "VideoPlayer.h"

int main(int argc, char** argv) {

  uint64_t cacheSize;
  if(argc == 3) {
    cacheSize = atol(argv[2]);
  } else if(argc == 2) {
    cacheSize = 2048;
  } else {
    printf("usage: video <filename> <cacheMB>\n");
    return 1;
  }

  // setup player
  VideoPlayer player(argv[1], cacheSize);

  // run player
  while(true) {
    player.playback();
  }

  return 0;
}