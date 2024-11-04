#define VESUVIUS_IMPL

#include "vesuvius-c.h"

int main(int argc, char** argv) {
  //Scroll 1 20230205180739
  volume* vol = volume_new((s32[3]){14376,7888,8096}, false, true, true, "./cache", 20230205180739);

  s32 start[3] = {2000,2000,2000};
  s32 dims[3] = {500,500,500};


  chunk* mychunk = volume_get_chunk(vol,start,dims);
  return 0;
}
