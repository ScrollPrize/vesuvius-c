#ifndef VESCUVIUS_H
#define VESCUVIUS_H

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <blosc2.h>

// Zarr config
#define ZARR_URL "https://dl.ash2txt.org/other/dev/scrolls/1/volumes/54keV_7.91um.zarr/0/"
#define CHUNK_SIZE_X 128
#define CHUNK_SIZE_Y 128
#define CHUNK_SIZE_Z 128
#define SHAPE_X 8096
#define SHAPE_Y 7888
#define SHAPE_Z 14376
#define CACHE_CAPACITY 100  // Define the LRU cache capacity

typedef struct {
    unsigned char *data;
    size_t size;
} MemoryChunk;

typedef struct LRUNode {
    int chunk_x;
    int chunk_y;
    int chunk_z;
    MemoryChunk chunk;
    struct LRUNode *prev;
    struct LRUNode *next;
} LRUNode;

typedef struct {
    LRUNode *head;
    LRUNode *tail;
    LRUNode *cache[CACHE_CAPACITY];
    int count;
} LRUCache;

// Function prototypes
size_t write_data(void *ptr, size_t size, size_t nmemb, MemoryChunk *chunk);
int fetch_zarr_chunk(int chunk_x, int chunk_y, int chunk_z, MemoryChunk *chunk);
int get_zarr_value(int x, int y, int z, unsigned char *value);

LRUCache *init_cache();
LRUNode *get_cache(LRUCache *cache, int chunk_x, int chunk_y, int chunk_z);
void put_cache(LRUCache *cache, int chunk_x, int chunk_y, int chunk_z, MemoryChunk chunk);
void move_to_head(LRUCache *cache, LRUNode *node);
void evict_from_cache(LRUCache *cache);
int hash_key(int chunk_x, int chunk_y, int chunk_z);

int fill_image_slice(int x_start, int x_end, int y_start, int y_end, int z, unsigned char *image);
int write_bmp(const char *filename, unsigned char *image, int width, int height);

void init_vescuvius();

// Global cache
LRUCache *cache;

// Initialize the vescuvius library
void init_vescuvius() {
    cache = init_cache();
}

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

// Initialize the LRU cache
LRUCache *init_cache() {
    LRUCache *cache = (LRUCache *)malloc(sizeof(LRUCache));
    cache->head = NULL;
    cache->tail = NULL;
    cache->count = 0;

    // Initialize all cache entries to NULL
    for (int i = 0; i < CACHE_CAPACITY; i++) {
        cache->cache[i] = NULL;
    }

    return cache;
}

// Hash function to generate a key for the cache
int hash_key(int chunk_x, int chunk_y, int chunk_z) {
    // Ensure the hash key is non-negative and within the bounds of CACHE_CAPACITY
    return abs((chunk_x * 73856093) ^ (chunk_y * 19349663) ^ (chunk_z * 83492791)) % CACHE_CAPACITY;
}

// Get chunk from the cache
LRUNode *get_cache(LRUCache *cache, int chunk_x, int chunk_y, int chunk_z) {
    int key = hash_key(chunk_x, chunk_y, chunk_z);
    LRUNode *node = cache->cache[key];

    // Check if the node matches the requested chunk coordinates
    if (node && node->chunk_x == chunk_x && node->chunk_y == chunk_y && node->chunk_z == chunk_z) {
        move_to_head(cache, node);  // Move the node to the head (most recently used)
        return node;
    }
    return NULL;
}

// Put a chunk into the cache
void put_cache(LRUCache *cache, int chunk_x, int chunk_y, int chunk_z, MemoryChunk chunk) {
    int key = hash_key(chunk_x, chunk_y, chunk_z);
    LRUNode *node = (LRUNode *)malloc(sizeof(LRUNode));

    node->chunk_x = chunk_x;
    node->chunk_y = chunk_y;
    node->chunk_z = chunk_z;
    node->chunk = chunk;
    node->prev = NULL;
    node->next = cache->head;

    if (cache->head != NULL) {
        cache->head->prev = node;
    }
    cache->head = node;

    if (cache->tail == NULL) {
        cache->tail = node;
    }

    // If the cache is full, evict the least recently used node
    if (cache->count == CACHE_CAPACITY) {
        evict_from_cache(cache);
    } else {
        cache->count++;
    }

    cache->cache[key] = node;
}

// Move a node to the head of the LRU cache (most recently used)
void move_to_head(LRUCache *cache, LRUNode *node) {
    if (node == cache->head) return;

    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }

    if (node == cache->tail) {
        cache->tail = node->prev;
    }

    node->prev = NULL;
    node->next = cache->head;

    if (cache->head != NULL) {
        cache->head->prev = node;
    }
    cache->head = node;
}

// Evict the least recently used node from the cache
void evict_from_cache(LRUCache *cache) {
    if (cache->tail == NULL) return;

    LRUNode *node = cache->tail;

    if (cache->tail->prev) {
        cache->tail->prev->next = NULL;
    }
    cache->tail = cache->tail->prev;

    int key = hash_key(node->chunk_x, node->chunk_y, node->chunk_z);
    cache->cache[key] = NULL;

    free(node->chunk.data);
    free(node);
}

// Function to get chunk data from the Zarr store or cache
int fetch_zarr_chunk(int chunk_x, int chunk_y, int chunk_z, MemoryChunk *chunk) {
    LRUNode *cached_node = get_cache(cache, chunk_x, chunk_y, chunk_z);
    if (cached_node) {
        *chunk = cached_node->chunk;
        return 0;
    }

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

        // Put the fetched chunk into the cache
        put_cache(cache, chunk_x, chunk_y, chunk_z, *chunk);
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
        return -1;
    }

    // Retrieve the value from the decompressed chunk data
    *value = decompressed_data[local_z * CHUNK_SIZE_X * CHUNK_SIZE_Y + local_y * CHUNK_SIZE_X + local_x];

    return 0;
}

// Function to fill an image slice from the 3D Zarr data
int fill_image_slice(int x_start, int x_end, int y_start, int y_end, int z, unsigned char *image) {
    // Validate boundaries
    if (x_start < 0 || x_end >= SHAPE_X || y_start < 0 || y_end >= SHAPE_Y || z < 0 || z >= SHAPE_Z) {
        fprintf(stderr, "Invalid boundaries for the slice\n");
        return -1;
    }

    int image_width = x_end - x_start + 1;
    int image_height = y_end - y_start + 1;

    // Determine the range of chunks needed for the slice
    int chunk_start_x = x_start / CHUNK_SIZE_X;
    int chunk_end_x = x_end / CHUNK_SIZE_X;
    int chunk_start_y = y_start / CHUNK_SIZE_Y;
    int chunk_end_y = y_end / CHUNK_SIZE_Y;

    int local_start_x, local_end_x, local_start_y, local_end_y;
    unsigned char decompressed_data[CHUNK_SIZE_Z * CHUNK_SIZE_Y * CHUNK_SIZE_X];

    // Loop over all chunks that cover the slice
    for (int chunk_y = chunk_start_y; chunk_y <= chunk_end_y; ++chunk_y) {
        for (int chunk_x = chunk_start_x; chunk_x <= chunk_end_x; ++chunk_x) {
            // Fetch the chunk data
            MemoryChunk chunk = {0};
            if (fetch_zarr_chunk(chunk_x, chunk_y, z / CHUNK_SIZE_Z, &chunk) != 0) {
                fprintf(stderr, "Failed to fetch Zarr chunk (%d, %d, %d)\n", chunk_x, chunk_y, z / CHUNK_SIZE_Z);
                return -1;
            }

            // Decompress the chunk using Blosc
            int decompressed_size = blosc2_decompress(chunk.data, chunk.size, decompressed_data, sizeof(decompressed_data));
            if (decompressed_size < 0) {
                fprintf(stderr, "Blosc2 decompression failed: %d\n", decompressed_size);
                free(chunk.data);
                return -1;
            }

            // Calculate local boundaries within the chunk
            local_start_x = (chunk_x == chunk_start_x) ? x_start % CHUNK_SIZE_X : 0;
            local_end_x = (chunk_x == chunk_end_x) ? x_end % CHUNK_SIZE_X : CHUNK_SIZE_X - 1;
            local_start_y = (chunk_y == chunk_start_y) ? y_start % CHUNK_SIZE_Y : 0;
            local_end_y = (chunk_y == chunk_end_y) ? y_end % CHUNK_SIZE_Y : CHUNK_SIZE_Y - 1;

            // Copy the relevant data from the decompressed chunk to the image slice
            for (int y = local_start_y; y <= local_end_y; ++y) {
                memcpy(&image[(chunk_y * CHUNK_SIZE_Y + y - y_start) * image_width + chunk_x * CHUNK_SIZE_X + local_start_x - x_start],
                       &decompressed_data[y * CHUNK_SIZE_X + local_start_x],
                       local_end_x - local_start_x + 1);
            }

            // Clean up
            free(chunk.data);
        }
    }

    return 0;
}

// BMP Header Structures
#pragma pack(push, 1) // Ensure no padding
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFileHeader;

typedef struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPInfoHeader;
#pragma pack(pop)

// Function to write the image slice to a BMP file
int write_bmp(const char *filename, unsigned char *image, int width, int height) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Failed to open file for writing: %s\n", filename);
        return -1;
    }

    // BMP file and info headers
    BMPFileHeader file_header;
    BMPInfoHeader info_header;

    // BMP file header
    file_header.bfType = 0x4D42; // 'BM'
    file_header.bfOffBits = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);
    file_header.bfSize = file_header.bfOffBits + width * height;
    file_header.bfReserved1 = 0;
    file_header.bfReserved2 = 0;

    // BMP info header
    info_header.biSize = sizeof(BMPInfoHeader);
    info_header.biWidth = width;
    info_header.biHeight = -height; // Negative height to indicate top-down row order
    info_header.biPlanes = 1;
    info_header.biBitCount = 8; // 8 bits per pixel (grayscale)
    info_header.biCompression = 0;
    info_header.biSizeImage = width * height;
    info_header.biXPelsPerMeter = 2835; // 72 DPI
    info_header.biYPelsPerMeter = 2835; // 72 DPI
    info_header.biClrUsed = 256;
    info_header.biClrImportant = 256;

    // Write BMP file header
    fwrite(&file_header, sizeof(BMPFileHeader), 1, file);

    // Write BMP info header
    fwrite(&info_header, sizeof(BMPInfoHeader), 1, file);

    // Write the grayscale palette (256 shades of gray)
    for (int i = 0; i < 256; ++i) {
        unsigned char color[4] = {i, i, i, 0}; // R, G, B, Reserved
        fwrite(color, sizeof(unsigned char), 4, file);
    }

    // Write the pixel data (image)
    size_t written = fwrite(image, sizeof(unsigned char), width * height, file);
    if (written != width * height) {
        fprintf(stderr, "Failed to write image data to file\n");
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}

#endif // VESCUVIUS_H
