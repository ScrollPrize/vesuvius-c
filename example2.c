#define VESUVIUS_IMPL

#include "vesuvius-c.h"

int main(int argc, char** argv) {
  //Scroll 1 20230205180739
  //volume* vol = vs_vol_new((s32[3]){14376,7888,8096}, false, true, true, "./cache", 20230205180739);

  //s32 start[3] = {2000,2000,2000};
  //s32 dims[3] = {500,500,500};


  chunk* mychunk;// = vs_vol_get_chunk(vol,start,dims);
  //if (mychunk == NULL) {
  //  return 1;
  //}
  //vs_chunk_free(mychunk);

  void* metadata_buf;
  char url[1024] = "https://dl.ash2txt.org/full-scrolls/Scroll1/PHercParis4.volpkg/volumes_zarr_standardized/54keV_7.91um_Scroll1A.zarr/0/.zarray";
  if (vs_download(url, &metadata_buf) <= 0) {
    return 1;
  }
  zarr_metadata metadata;
  if (vs_zarr_parse_metadata(metadata_buf, &metadata)) {
    return 1;
  }
  void* compressed_buf;
  strncpy(url,"https://dl.ash2txt.org/full-scrolls/Scroll1/PHercParis4.volpkg/volumes_zarr_standardized/54keV_7.91um_Scroll1A.zarr/0/50/30/30",1023);
  long compressed_size;
  if ((compressed_size = vs_download(url, &compressed_buf)) <= 0) {
    return 1;
  }
  mychunk = vs_zarr_decompress_chunk(compressed_size, compressed_buf,metadata);
  if (mychunk == NULL) {
    return 1;
  }
  chunk* rescaled = vs_normalize_chunk(mychunk);
  s32 vertex_count, index_count;
  f32 *vertices;
  s32 *indices;

  if (vs_march_cubes(mychunk->data,mychunk->dims[0],mychunk->dims[1],mychunk->dims[2],.5f,&vertices,&indices,&vertex_count,&index_count)) {
    return 1;
  }

  if (vs_ply_write("out.ply",vertices,NULL,indices,vertex_count,index_count)) {
    return 1;
  }

  return 0;
}
