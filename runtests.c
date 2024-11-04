#define VESUVIUS_IMPL
#include "vesuvius-c.h"

int testcurl() {
#ifndef VESUVIUS_CURL_IMPL
  printf("vesuvius was not built with curl support. skipping test\n");
  return 0;
#else
  printf("%s\n",__FUNCTION__);

  char* buf;
  long len = download("https://dl.ash2txt.org/full-scrolls/Scroll1/PHercParis4.volpkg/paths/20230503225234/author.txt", &buf);
  if(len!= 6) {
    return 1;
  }
  if (strncmp(buf,"noemi",5) != 0) {return 1;}
  free(buf);
  return 0;
#endif
}

int testhistogram() {
  printf("%s\n", __FUNCTION__);

  chunk* mychunk = tiff_to_chunk("./img/example_3d.tif");
  if (mychunk == NULL) { return 1; }
  slice* myslice = tiff_to_slice("./img/example_3d.tif", 0);
  if (myslice == NULL) { return 1; }
  histogram* slice_hist = slice_histogram(myslice->data, myslice->dims[0], myslice->dims[1], 256);
  if (slice_hist == NULL) { return 1; }
  histogram* chunk_hist = chunk_histogram(mychunk->data, mychunk->dims[0], mychunk->dims[1], mychunk->dims[2], 256);
  if (chunk_hist == NULL) { return 1; }
  hist_stats stats = calculate_histogram_stats(slice_hist);
  printf("Mean: %.2f\n", stats.mean);
  printf("Median: %.2f\n", stats.median);
  printf("Mode: %.2f (count: %u)\n", stats.mode, stats.mode_count);
  printf("Standard Deviation: %.2f\n", stats.std_dev);

  if (write_histogram_to_csv(slice_hist, "slice_histogram.csv")) { return 1; }
  if (write_histogram_to_csv(chunk_hist, "chunk_histogram.csv")) { return 1; }

  histogram_free(slice_hist);
  histogram_free(chunk_hist);
  chunk_free(mychunk);
  slice_free(myslice);
  return 0;
}

int testzarr() {
#ifndef VESUVIUS_ZARR_IMPL
  printf("vesuvius was not built with zarr support. skipping testzarr\n");
  return 0;
#else
  printf("zarr support not yet implemented\n");
  return 0;
  /*
  printf("%s\n",__FUNCTION__);

  zarr_metadata metadata = parse_zarray("./example_data/test.zarray");
  int z = metadata.shape[0];
  int y = metadata.shape[1];
  int x = metadata.shape[2];
  int dtype_size = 0;
  if(strcmp(metadata.dtype,"|u1") == 0) {
    dtype_size = 1;
  } else {
    return 1;
    assert(false);
  }

  FILE* fp = fopen("./example_data/30", "rb");
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  assert(size < 1024*1024*1024);
  u8* compressed_data = malloc(size);


  unsigned char *decompressed_data = malloc(z * y * x * dtype_size);
  int decompressed_size = blosc2_decompress(compressed_data, size, decompressed_data, z*y*x);
  if (decompressed_size < 0) {
    fprintf(stderr, "Blosc2 decompression failed: %d\n", decompressed_size);
    free(compressed_data);
    free(decompressed_data);
    return 1;
  }
  printf("asdf\n");
  return 0;
  */
#endif
}

int testmesher() {
  printf("%s\n", __FUNCTION__);

  chunk* mychunk = tiff_to_chunk("./img/example_3d.tif");
  if (mychunk == NULL) { return 1; }
  chunk* smallerchunk = sumpool(mychunk, 2, 2);
  if (smallerchunk == NULL) { return 1; }
  chunk* rescaled = normalize_chunk(smallerchunk);
  if (rescaled == NULL) { return 1; }
  float* vertices;
  int* indices;
  int vertex_count, indices_count;
  int ret = march_cubes(rescaled->data, rescaled->dims[0], rescaled->dims[1], rescaled->dims[2], 0.5f, &vertices,
                        &indices, &vertex_count, &indices_count);
  if (ret != 0) { return 1; }
  ret = write_ply("mymesh.ply", vertices,NULL, indices, vertex_count, indices_count);
  if (ret != 0) { return 1; }
  chunk_free(rescaled);
  chunk_free(mychunk);
  chunk_free(smallerchunk);
  return 0;
}

int testmath() {
  printf("%s\n", __FUNCTION__);

  chunk* mychunk = chunk_new((s32[3]){128, 128, 128});
  if (mychunk == NULL) { return 1; }
  for (int z = 0; z < 128; z++) {
    for (int y = 0; y < 128; y++) { for (int x = 0; x < 128; x++) { chunk_set(mychunk, z, y, x, 1.0f); } }
  }
  chunk* smaller = sumpool(mychunk, 2, 2);
  if (smaller == NULL) { return 1; }
  if (smaller->dims[0] != 64 || smaller->dims[1] != 64 || smaller->dims[2] != 64) { return 1; }
  for (int z = 0; z < 64; z++) {
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        f32 val = chunk_get(smaller, z, y, x);
        if (val > 8.01f || val < 7.99f) { return 1; }
      }
    }
  }
  return 0;
}

int testvcps() {
  printf("%s\n", __FUNCTION__);

  size_t width = 2, height = 2, dim = 3;
  size_t total_points = width * height * dim;

  // Test float->double->float conversion
  float* test_float_data = malloc(total_points * sizeof(float));
  for (size_t i = 0; i < total_points; i++) { test_float_data[i] = (float)i + 0.5f; }

  // Write float data as double
  if (write_vcps("test_double.vcps", width, height, dim, test_float_data, "float", "double")) {
    fprintf(stderr, "Failed to write float->double test file\n");
    free(test_float_data);
    return 1;
  }

  // Read it back as float
  float* read_float_data = malloc(total_points * sizeof(float));
  size_t read_width, read_height, read_dim;

  int read_status = read_vcps("test_double.vcps", &read_width, &read_height, &read_dim, read_float_data, "float");
  if (read_status) {
    fprintf(stderr, "Failed to read double->float test file (status=%d)\n", read_status);
    free(test_float_data);
    free(read_float_data);
    return 1;
  }

  // Verify dimensions
  if (width != read_width || height != read_height || dim != read_dim) {
    fprintf(stderr, "Dimension mismatch: expected (%zux%zux%zu), got (%zux%zux%zu)\n",
            width, height, dim, read_width, read_height, read_dim);
    free(test_float_data);
    free(read_float_data);
    return 1;
  }

  // Verify float data
  int float_test_passed = 1;
  for (size_t i = 0; i < total_points; i++) {
    if (fabsf(test_float_data[i] - read_float_data[i]) > 1e-6f) {
      fprintf(stderr, "Float data mismatch at %zu: %f != %f\n",
              i, test_float_data[i], read_float_data[i]);
      float_test_passed = 0;
      break;
    }
  }
  free(test_float_data);
  free(read_float_data);

  return float_test_passed ? 0 : 1;
}

int testchamfer() {
  printf("%s\n", __FUNCTION__);
  float *vertices1, *vertices2;
  float *normals1, *normals2;
  int *indices1, *indices2;
  int vertex_count1, vertex_count2;
  int index_count1, index_count2;
  int normal_count1, normal_count2;


  if (read_ply("./example_data/cell_yxz_002_007_070.ply", &vertices1, &normals1, &indices1, &vertex_count1,
               &normal_count1, &index_count1) != 0) {
    printf("Failed to read first mesh\n");
    return 1;
  }

  if (read_ply("./example_data/cell_yxz_002_007_071.ply", &vertices2, &normals2, &indices2, &vertex_count2,
               &normal_count2, &index_count2) != 0) {
    printf("Failed to read second mesh\n");
    free(vertices1);
    free(indices1);
    return 1;
  }

  float distance = chamfer_distance(vertices1, vertex_count1,
                                    vertices2, vertex_count2);

  printf("Chamfer distance between meshes: %f\n", distance);

  // Clean up
  free(vertices1);
  free(vertices2);
  free(indices1);
  free(indices2);
  return 0;
}

int main(int argc, char** argv) {
  if (testcurl())      printf("testcurl failed\n");
  if (testzarr())      printf("testzarr failed\n");
  if (testhistogram()) printf("testhistogram failed\n");
  if (testmesher())    printf("testmesher failed\n");
  if (testmath())      printf("testmath failed\n");
  if (testvcps())      printf("testvcps failed\n");
  if (testchamfer())   printf("testchamfer failed\n");
  printf("Hello World\n");


  return 0;
}
