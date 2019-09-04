#include <stdio.h>
#include "VideoPlayer.h"

int main(int argc, char** argv) {

  // check arguments
  if(argc != 3) {
    printf("usage: video <filename> <cacheMB>\n");
    return 1;
  }

  // setup player
  VideoPlayer player(argv[1], atol(argv[2]));

  // run player
  while(true) {
    player.playback();
  }

  return 0;
}