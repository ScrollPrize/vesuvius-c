#define VESUVIUS_IMPL

#include "vesuvius-c.h"

int test_volume_load() {
    const char* zarray_url = "https://dl.ash2txt.org/full-scrolls/Scroll1/PHercParis4.volpkg/volumes_zarr_standardized/54keV_7.91um_Scroll1A.zarr/0/.zarray";
    const char* zarr_block_url = "https://dl.ash2txt.org/full-scrolls/Scroll1/PHercParis4.volpkg/volumes_zarr_standardized/54keV_7.91um_Scroll1A.zarr/0/50/30/30";
    void* metadata_buf;
    printf("downloading %s\n",zarray_url);
    if (vs_download(zarray_url, &metadata_buf) <= 0) {
        return 1;
    }

    zarr_metadata metadata;
    printf("parsing zarr metadata\n");
    if (vs_zarr_parse_metadata(metadata_buf, &metadata)) {
        return 1;
    }

    void* compressed_buf;
    long compressed_size;
    printf("downloading %s\n",zarr_block_url);
    if ((compressed_size = vs_download(zarr_block_url, &compressed_buf)) <= 0) {
        return 1;
    }
    printf("decompressing zarr chunk\n");
    chunk* mychunk = vs_zarr_decompress_chunk(compressed_size, compressed_buf,metadata);
    if (mychunk == NULL) {
        return 1;
    }
    printf("rescaling zarr chunk\n");
    chunk* rescaled = vs_normalize_chunk(mychunk);
    s32 vertex_count, index_count;
    f32 *vertices;
    s32 *indices;
    printf("marching cubes on rescaled chunk\n");
    if (vs_march_cubes(rescaled->data,rescaled->dims[0],rescaled->dims[1],rescaled->dims[2],.5f,&vertices,&indices,&vertex_count,&index_count)) {
        return 1;
    }
    printf("writing mesh to out_vol.ply\n");
    if (vs_ply_write("out_vol.ply",vertices,NULL,indices,vertex_count,index_count)) {
        return 1;
    }

    return 0;
}

int test_fiber_load() {
    const char* zarray_url = "https://dl.ash2txt.org/community-uploads/bruniss/Fiber-and-Surface-Models/GP-Predictions/updated_zarrs/mask-2ext-surface_ome.zarr/0/.zarray";
    const char* zarr_block_url = "https://dl.ash2txt.org/community-uploads/bruniss/Fiber-and-Surface-Models/GP-Predictions/updated_zarrs/mask-2ext-surface_ome.zarr/0/50/30/30";

    chunk* mychunk;

    void* metadata_buf;
    printf("downloading %s\n",zarray_url);
    if (vs_download(zarray_url, &metadata_buf) <= 0) {
        return 1;
    }
    zarr_metadata metadata;
    printf("parsing zarray metadata\n");
    if (vs_zarr_parse_metadata(metadata_buf, &metadata)) {
        return 1;
    }
    void* compressed_buf;
    long compressed_size;
    printf("downloading %s\n",zarr_block_url);
    if ((compressed_size = vs_download(zarr_block_url, &compressed_buf)) <= 0) {
        return 1;
    }
    printf("decompressing zarr chunk\n");
    mychunk = vs_zarr_decompress_chunk(compressed_size, compressed_buf,metadata);
    if (mychunk == NULL) {
        return 1;
    }
    printf("rescaling zarr chunk\n");
    chunk* rescaled = vs_normalize_chunk(mychunk);
    s32 vertex_count, index_count;
    f32 *vertices;
    s32 *indices;
    printf("marching cubes on rescaled chunk\n");
    if (vs_march_cubes(rescaled->data,rescaled->dims[0],rescaled->dims[1],rescaled->dims[2],.5f,&vertices,&indices,&vertex_count,&index_count)) {
        return 1;
    }
    printf("writing mesh to out_surface.ply\n");
    if (vs_ply_write("out_surface.ply",vertices,NULL,indices,vertex_count,index_count)) {
        return 1;
    }

    return 0;
}

int main(int argc, char** argv) {
  if (test_volume_load()) {
    return 1;
  }
  if (test_fiber_load()) {
    return 1;
  }
  return 0;
}
