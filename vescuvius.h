#ifndef ZARR_READER_H
#define ZARR_READER_H

#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <blosc2.h>

// Configuration for your specific Zarr array
#define ZARR_URL "https://dl.ash2txt.org/other/dev/scrolls/1/volumes/54keV_7.91um.zarr/0/"
#define CHUNK_SIZE_X 128
#define CHUNK_SIZE_Y 128
#define CHUNK_SIZE_Z 128
#define SHAPE_X 8096
#define SHAPE_Y 7888
#define SHAPE_Z 14376

typedef struct {
    unsigned char *data;
    size_t size;
} MemoryChunk;

// Function to write data to memory (used by curl)
size_t write_data(void *ptr, size_t size, size_t nmemb, MemoryChunk *chunk) {
    size_t realsize = size * nmemb;
    chunk->data = (unsigned char *)realloc(chunk->data, chunk->size + realsize);
    if (chunk->data == NULL) {
        fprintf(stderr, "Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    memcpy(&(chunk->data[chunk->size]), ptr, realsize);
    chunk->size += realsize;
    return realsize;
}

// Function to get chunk data from the Zarr store
int fetch_zarr_chunk(int chunk_x, int chunk_y, int chunk_z, MemoryChunk *chunk) {
    CURL *curl;
    CURLcode res;
    char url[512];

    snprintf(url, sizeof(url), "%s%d/%d/%d", ZARR_URL, chunk_z, chunk_y, chunk_x);
    chunk->data = (unsigned char *) malloc(1);  
    chunk->size = 0;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            return -1;
        }
        curl_easy_cleanup(curl);
    }
    return 0;
}

// Function to retrieve the value at a specific (x, y, z) index
int get_zarr_value(int x, int y, int z, unsigned char *value) {
    // Calculate the corresponding chunk indices
    int chunk_x = x / CHUNK_SIZE_X;
    int chunk_y = y / CHUNK_SIZE_Y;
    int chunk_z = z / CHUNK_SIZE_Z;

    // Calculate the local indices within the chunk
    int local_x = x % CHUNK_SIZE_X;
    int local_y = y % CHUNK_SIZE_Y;
    int local_z = z % CHUNK_SIZE_Z;

    // Fetch the chunk data
    MemoryChunk chunk = {0};
    if (fetch_zarr_chunk(chunk_x, chunk_y, chunk_z, &chunk) != 0) {
        fprintf(stderr, "Failed to fetch Zarr chunk\n");
        return -1;
    }

    // Decompress the chunk using Blosc
    unsigned char decompressed_data[CHUNK_SIZE_Z * CHUNK_SIZE_Y * CHUNK_SIZE_X];
    int decompressed_size = blosc2_decompress(chunk.data, chunk.size, decompressed_data, sizeof(decompressed_data));
    if (decompressed_size < 0) {
        fprintf(stderr, "Blosc2 decompression failed: %d\n", decompressed_size);
        free(chunk.data);
        return -1;
    }

    // Retrieve the value from the decompressed chunk data
    *value = decompressed_data[local_z * CHUNK_SIZE_X * CHUNK_SIZE_Y + local_y * CHUNK_SIZE_X + local_x];

    // Clean up
    free(chunk.data);

    return 0;
}

#endif // ZARR_READER_H