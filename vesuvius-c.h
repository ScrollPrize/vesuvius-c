#ifndef VESUVIUS_H
#define VESUVIUS_H

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <blosc2.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <float.h>

// Variables to hold Zarr's chunk sizes and shape, initially set to -1 to indicate uninitialized
int CHUNK_SIZE_X = -1, CHUNK_SIZE_Y = -1, CHUNK_SIZE_Z = -1;
int SHAPE_X = -1, SHAPE_Y = -1, SHAPE_Z = -1;

// Buffer size for metadata JSON and URL
#define BUFFER_SIZE 4096
#define URL_SIZE 256

// Global variable to store the dynamically constructed Zarr URL
char ZARR_URL[URL_SIZE] = {0};  // Initially empty

#define CACHE_CAPACITY 100  // Define the LRU cache capacity
#define CACHE_DIR ".vesuvius-cache"

// Struct for scroll volume regions
typedef struct {
    int x_start;
    int x_width;
    int y_start;
    int y_height;
    int z_start;
    int z_depth;
} RegionOfInterest;

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

typedef struct {
    float x, y, z;
} Vertex;

typedef struct {
    int v1, v2, v3;  // Indices of the triangle's vertices
} Triangle;

typedef struct {
    Vertex *vertices;
    Triangle *triangles;
    size_t vertex_count;
    size_t triangle_count;
} TriangleMesh;

// Function prototypes
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *stream);
static int fetch_metadata(const char *url, char *buffer);
static int parse_metadata(const char *buffer);
int load_shape_and_chunksize(void);

void init_vesuvius(const char *scroll_id, int energy, double resolution);

LRUCache *init_cache();
LRUNode *get_cache(LRUCache *cache, int chunk_x, int chunk_y, int chunk_z);
void put_cache(LRUCache *cache, int chunk_x, int chunk_y, int chunk_z, MemoryChunk chunk);
void move_to_head(LRUCache *cache, LRUNode *node);
void evict_from_cache(LRUCache *cache);
int hash_key(int chunk_x, int chunk_y, int chunk_z);

size_t write_data(void *ptr, size_t size, size_t nmemb, MemoryChunk *chunk);

int fetch_zarr_chunk(int chunk_x, int chunk_y, int chunk_z, MemoryChunk *chunk);

int get_volume_voxel(int x, int y, int z, unsigned char *value);
int get_volume_roi(RegionOfInterest region, unsigned char *volume);
int get_volume_slice(RegionOfInterest region, unsigned char *slice);

int write_bmp(const char *filename, unsigned char *image, int width, int height);
int create_directories(const char *path);
int write_chunk_to_disk(int chunk_x, int chunk_y, int chunk_z, MemoryChunk *chunk);
int read_chunk_from_disk(int chunk_x, int chunk_y, int chunk_z, MemoryChunk *chunk);
char *get_cache_path(int chunk_x, int chunk_y, int chunk_z);

char *get_obj_cache_path(const char *id);
int download_obj_file(const char *id, const char *cache_path);
int fetch_obj_file(const char *id, char **obj_file_path);
int parse_obj_file(const char *file_path, TriangleMesh *mesh);
int get_triangle_mesh(const char *id, TriangleMesh *mesh);
int write_trianglemesh_to_obj(const char *filename, const TriangleMesh *mesh);
RegionOfInterest get_mesh_bounding_box(const TriangleMesh *mesh);
void reset_mesh_origin_to_roi(TriangleMesh *mesh, const RegionOfInterest *roi);

// Global cache
LRUCache *cache;

// Internal function to write data fetched by cURL
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    strncat((char *)stream, (char *)ptr, size * nmemb);
    return size * nmemb;
}

// Fetches the metadata JSON from the specified Zarr directory URL
static int fetch_metadata(const char *url, char *buffer) {
    CURL *curl;
    CURLcode res;

    // Construct the full URL to the `.zarray` metadata file
    char metadata_url[URL_SIZE];
    snprintf(metadata_url, URL_SIZE, "%s.zarray", url);

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, metadata_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            return -1;
        }
    } else {
        fprintf(stderr, "Failed to initialize curl\n");
        return -1;
    }
    return 0;
}

// Parses the metadata JSON to retrieve chunk sizes and shape
static int parse_metadata(const char *buffer) {
    struct json_object *parsed_json, *chunks, *shape;

    parsed_json = json_tokener_parse(buffer);
    if (parsed_json == NULL) {
        fprintf(stderr, "Failed to parse JSON\n");
        return -1;
    }

    // Extract "chunks" and "shape" arrays from JSON
    if (!json_object_object_get_ex(parsed_json, "chunks", &chunks) ||
        !json_object_object_get_ex(parsed_json, "shape", &shape)) {
        fprintf(stderr, "Missing 'chunks' or 'shape' in metadata\n");
        json_object_put(parsed_json);
        return -1;
    }

    // Set chunk sizes from "chunks" array
    CHUNK_SIZE_Z = json_object_get_int(json_object_array_get_idx(chunks, 0));
    CHUNK_SIZE_Y = json_object_get_int(json_object_array_get_idx(chunks, 1));
    CHUNK_SIZE_X = json_object_get_int(json_object_array_get_idx(chunks, 2));

    // Set shape sizes from "shape" array
    SHAPE_Z = json_object_get_int(json_object_array_get_idx(shape, 0));
    SHAPE_Y = json_object_get_int(json_object_array_get_idx(shape, 1));
    SHAPE_X = json_object_get_int(json_object_array_get_idx(shape, 2));

    json_object_put(parsed_json);  // Free JSON object
    return 0;
}

// Public function to initialize chunk sizes and shape
int load_shape_and_chunksize() {
    char buffer[BUFFER_SIZE] = {0};

    if (fetch_metadata(ZARR_URL, buffer) != 0) {
        fprintf(stderr, "Failed to fetch metadata\n");
        return -1;
    }
    if (parse_metadata(buffer) != 0) {
        fprintf(stderr, "Failed to parse metadata\n");
        return -1;
    }

    return 0;
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

// Initialize the vesuvius library with dynamic URL construction
void init_vesuvius(const char *scroll_id, int energy, double resolution) {
    // Construct the ZARR_URL based on the provided parameters, stopping at the directory
    snprintf(ZARR_URL, URL_SIZE, 
             "https://dl.ash2txt.org/other/dev/scrolls/%s/volumes/%dkeV_%.2fum.zarr/0/", 
             scroll_id, energy, resolution);

    // Load shape and chunk size from the dynamically constructed ZARR_URL
    if (load_shape_and_chunksize() != 0) {
        fprintf(stderr, "Failed to load shape and chunk size\n");
        exit(EXIT_FAILURE);  // Exit if metadata loading fails
    }

    // Print shape and chunk size to verify
    printf("Loaded Zarr metadata from: %s\n", ZARR_URL);
    printf("Shape: Z=%d, Y=%d, X=%d\n", SHAPE_Z, SHAPE_Y, SHAPE_X);
    printf("Chunk Size: Z=%d, Y=%d, X=%d\n", CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z);

    // Initialize cache (assuming init_cache() initializes the cache system)
    cache = init_cache();
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

// Get path for disk cache based on chunk coordinates
char *get_cache_path(int chunk_x, int chunk_y, int chunk_z) {
    char *path = (char *)malloc(512 * sizeof(char));
    snprintf(path, 512, "%s/other/dev/scrolls/1/volumes/54keV_7.91um.zarr/0/%d/%d/%d", CACHE_DIR, chunk_z, chunk_y, chunk_x);
    return path;
}

// Helper function to create directories recursively
int create_directories(const char *path) {
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s", path);

    for (char *p = temp_path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';  // Temporarily terminate the string
            if (mkdir(temp_path, 0755) != 0 && errno != EEXIST) {
                return -1;  // Failed to create directory
            }
            *p = '/';  // Restore the original slash
        }
    }

    // Create the last directory in the path
    if (mkdir(temp_path, 0755) != 0 && errno != EEXIST) {
        return -1;  // Failed to create final directory
    }

    return 0;  // Success
}

int write_chunk_to_disk(int chunk_x, int chunk_y, int chunk_z, MemoryChunk *chunk) {
    char *path = get_cache_path(chunk_x, chunk_y, chunk_z);

    // Ensure the directory structure is created recursively
    char *dir = strdup(path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';  // Remove the file name, keeping only the directory path
        if (create_directories(dir) != 0) {
            fprintf(stderr, "Failed to create directory: %s\n", dir);
            free(dir);
            free(path);
            return -1;
        }
    }
    free(dir);

    // Write chunk data to disk
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        free(path);
        return -1;
    }

    fwrite(chunk->data, sizeof(unsigned char), chunk->size, file);
    fclose(file);
    free(path);

    return 0;
}


// Read a chunk from disk cache
int read_chunk_from_disk(int chunk_x, int chunk_y, int chunk_z, MemoryChunk *chunk) {
    char *path = get_cache_path(chunk_x, chunk_y, chunk_z);

    FILE *file = fopen(path, "rb");
    if (!file) {
        free(path);
        return -1; // File does not exist
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    chunk->data = (unsigned char *)malloc(file_size);
    if (chunk->data == NULL) {
        fclose(file);
        free(path);
        return -1;
    }

    fread(chunk->data, sizeof(unsigned char), file_size, file);
    chunk->size = file_size;

    fclose(file);
    free(path);
    return 0;
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

// Get chunk from the cache, disk, or fetch it
int fetch_zarr_chunk(int chunk_x, int chunk_y, int chunk_z, MemoryChunk *chunk) {
    LRUNode *cached_node = get_cache(cache, chunk_x, chunk_y, chunk_z);
    if (cached_node) {
        *chunk = cached_node->chunk;
        return 0;
    }

    // Try reading from disk cache
    if (read_chunk_from_disk(chunk_x, chunk_y, chunk_z, chunk) == 0) {
        put_cache(cache, chunk_x, chunk_y, chunk_z, *chunk);  // Store in memory cache
        return 0;
    }

    // Fetch the chunk from the server
    CURL *curl;
    CURLcode res;
    char url[512];

    snprintf(url, sizeof(url), "%s%d/%d/%d", ZARR_URL, chunk_z, chunk_y, chunk_x);
    chunk->data = (unsigned char *)malloc(1);
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

        // Decompress the chunk using Blosc
        unsigned char *decompressed_data = (unsigned char *)malloc(CHUNK_SIZE_Z * CHUNK_SIZE_Y * CHUNK_SIZE_X);
        int decompressed_size = blosc2_decompress(chunk->data, chunk->size, decompressed_data, CHUNK_SIZE_Z * CHUNK_SIZE_Y * CHUNK_SIZE_X);
        if (decompressed_size < 0) {
            fprintf(stderr, "Blosc2 decompression failed: %d\n", decompressed_size);
            free(chunk->data);
            free(decompressed_data);
            return -1;
        }

        // Free the compressed data and update the chunk with the decompressed data
        free(chunk->data);
        chunk->data = decompressed_data;
        chunk->size = decompressed_size;

        // Store in memory and disk cache
        put_cache(cache, chunk_x, chunk_y, chunk_z, *chunk);
        write_chunk_to_disk(chunk_x, chunk_y, chunk_z, chunk);
    }
    return 0;
}

// Function to retrieve the value at a specific (x, y, z) index
int get_volume_voxel(int x, int y, int z, unsigned char *value) {
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

    // Retrieve the value from the chunk data
    *value = chunk.data[local_z * CHUNK_SIZE_X * CHUNK_SIZE_Y + local_y * CHUNK_SIZE_X + local_x];

    return 0;
}

// Function to fill a 3D volume from the Zarr data
int get_volume_roi(RegionOfInterest region, unsigned char *volume) {
    // Validate boundaries
    if (region.x_start < 0 || region.x_start + region.x_width > SHAPE_X ||
        region.y_start < 0 || region.y_start + region.y_height > SHAPE_Y ||
        region.z_start < 0 || region.z_start + region.z_depth > SHAPE_Z) {
        fprintf(stderr, "Invalid boundaries for the volume\n");
        return -1;
    }

    // Determine the range of chunks needed for the volume
    int chunk_start_x = region.x_start / CHUNK_SIZE_X;
    int chunk_end_x = (region.x_start + region.x_width - 1) / CHUNK_SIZE_X;
    int chunk_start_y = region.y_start / CHUNK_SIZE_Y;
    int chunk_end_y = (region.y_start + region.y_height - 1) / CHUNK_SIZE_Y;
    int chunk_start_z = region.z_start / CHUNK_SIZE_Z;
    int chunk_end_z = (region.z_start + region.z_depth - 1) / CHUNK_SIZE_Z;

    // Loop over all chunks that cover the volume
    for (int chunk_z = chunk_start_z; chunk_z <= chunk_end_z; ++chunk_z) {
        for (int chunk_y = chunk_start_y; chunk_y <= chunk_end_y; ++chunk_y) {
            for (int chunk_x = chunk_start_x; chunk_x <= chunk_end_x; ++chunk_x) {
                // Fetch the chunk data
                MemoryChunk chunk = {0};
                if (fetch_zarr_chunk(chunk_x, chunk_y, chunk_z, &chunk) != 0) {
                    fprintf(stderr, "Failed to fetch Zarr chunk (%d, %d, %d)\n", chunk_x, chunk_y, chunk_z);
                    return -1;
                }

                // Calculate local boundaries within the chunk
                int local_start_x = (chunk_x == chunk_start_x) ? region.x_start % CHUNK_SIZE_X : 0;
                int local_end_x = (chunk_x == chunk_end_x) ? (region.x_start + region.x_width - 1) % CHUNK_SIZE_X : CHUNK_SIZE_X - 1;
                int local_start_y = (chunk_y == chunk_start_y) ? region.y_start % CHUNK_SIZE_Y : 0;
                int local_end_y = (chunk_y == chunk_end_y) ? (region.y_start + region.y_height - 1) % CHUNK_SIZE_Y : CHUNK_SIZE_Y - 1;
                int local_start_z = (chunk_z == chunk_start_z) ? region.z_start % CHUNK_SIZE_Z : 0;
                int local_end_z = (chunk_z == chunk_end_z) ? (region.z_start + region.z_depth - 1) % CHUNK_SIZE_Z : CHUNK_SIZE_Z - 1;

                // Copy the relevant data from the chunk to the volume
                for (int z = local_start_z; z <= local_end_z; ++z) {
                    for (int y = local_start_y; y <= local_end_y; ++y) {
                        memcpy(&volume[((chunk_z * CHUNK_SIZE_Z + z - region.z_start) * region.y_height +
                                        (chunk_y * CHUNK_SIZE_Y + y - region.y_start)) * region.x_width +
                                       (chunk_x * CHUNK_SIZE_X + local_start_x - region.x_start)],
                               &chunk.data[z * CHUNK_SIZE_X * CHUNK_SIZE_Y + y * CHUNK_SIZE_X + local_start_x],
                               local_end_x - local_start_x + 1);
                    }
                }
            }
        }
    }

    return 0;
}

int get_volume_slice(RegionOfInterest region, unsigned char *slice) {
    // Validate boundaries
    if (region.x_start < 0 || region.x_start + region.x_width > SHAPE_X ||
        region.y_start < 0 || region.y_start + region.y_height > SHAPE_Y ||
        region.z_start < 0 || region.z_start + region.z_depth > SHAPE_Z) {
        fprintf(stderr, "Invalid boundaries for the volume\n");
        return -1;
    }

    // Validate depth 1
    if (region.z_depth != 1) {
        fprintf(stderr, "Slice must have z_depth of 1\n");
        return -1;
    }

    // Fetch the volume data for the slice
    unsigned char *volume = (unsigned char *)malloc(region.x_width * region.y_height);
    if (get_volume_roi(region, volume) != 0) {
        fprintf(stderr, "Failed to fetch volume data for slice\n");
        return -1;
    }

    // Copy the slice data from the volume
    for (int y = 0; y < region.y_height; y++) {
        for (int x = 0; x < region.x_width; x++) {
            slice[y * region.x_width + x] = volume[y * region.x_width + x];
        }
    }

    free(volume);
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

    // Calculate the size of each row including padding
    int rowSize = (width + 3) & ~3; // Round up to the nearest multiple of 4
    int imageSize = rowSize * height;

    // BMP file header
    file_header.bfType = 0x4D42; // 'BM'
    file_header.bfOffBits = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + 256 * 4; // File header + Info header + Palette
    file_header.bfSize = file_header.bfOffBits + imageSize;
    file_header.bfReserved1 = 0;
    file_header.bfReserved2 = 0;

    // BMP info header
    info_header.biSize = sizeof(BMPInfoHeader);
    info_header.biWidth = width;
    info_header.biHeight = -height; // Negative height to indicate top-down row order
    info_header.biPlanes = 1;
    info_header.biBitCount = 8; // 8 bits per pixel (grayscale)
    info_header.biCompression = 0;
    info_header.biSizeImage = imageSize;
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

    // Write the pixel data with padding
    unsigned char *row = (unsigned char *)malloc(rowSize);
    if (!row) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(file);
        return -1;
    }

    for (int y = 0; y < height; ++y) {
        // Copy the image row and pad it
        memcpy(row, image + (height - 1 - y) * width, width);
        memset(row + width, 0, rowSize - width); // Pad the row

        // Write the padded row
        fwrite(row, sizeof(unsigned char), rowSize, file);
    }

    free(row);
    fclose(file);
    return 0;
}

char *get_obj_cache_path(const char *id) {
    char *path = (char *)malloc(512 * sizeof(char));
    snprintf(path, 512, "%s/full-scrolls/Scroll1/PHercParis4.volpkg/paths/%s/%s.obj", CACHE_DIR, id, id);
    return path;
}

int download_obj_file(const char *id, const char *cache_path) {
    CURL *curl;
    CURLcode res;
    char url[512];

    snprintf(url, sizeof(url), "https://dl.ash2txt.org/full-scrolls/Scroll1/PHercParis4.volpkg/paths/%s/%s.obj", id, id);
    FILE *file = fopen(cache_path, "wb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", cache_path);
        return -1;
    }

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            fclose(file);
            curl_easy_cleanup(curl);
            return -1;
        }
        curl_easy_cleanup(curl);
    }
    fclose(file);
    return 0;
}

int fetch_obj_file(const char *id, char **obj_file_path) {
    char *cache_path = get_obj_cache_path(id);

    // Check if file exists in cache
    FILE *file = fopen(cache_path, "r");
    if (file) {
        fclose(file);
        *obj_file_path = cache_path;
        return 0; // File already cached
    }

    // Create directory structure and download the file
    char *dir = strdup(cache_path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (create_directories(dir) != 0) {
            fprintf(stderr, "Failed to create directory: %s\n", dir);
            free(dir);
            free(cache_path);
            return -1;
        }
    }
    free(dir);

    // Download the file
    if (download_obj_file(id, cache_path) != 0) {
        free(cache_path);
        return -1;
    }

    *obj_file_path = cache_path;
    return 0;
}

int parse_obj_file(const char *file_path, TriangleMesh *mesh) {
    FILE *file = fopen(file_path, "r");
    if (!file) {
        fprintf(stderr, "Failed to open .obj file: %s\n", file_path);
        return -1;
    }

    // Initial capacities for vertices and triangles
    size_t vertex_capacity = 100;
    size_t triangle_capacity = 100;

    // Allocate memory for vertices and triangles
    mesh->vertices = (Vertex *)malloc(vertex_capacity * sizeof(Vertex));
    mesh->triangles = (Triangle *)malloc(triangle_capacity * sizeof(Triangle));
    mesh->vertex_count = 0;
    mesh->triangle_count = 0;

    if (!mesh->vertices || !mesh->triangles) {
        fprintf(stderr, "Failed to allocate memory for mesh.\n");
        fclose(file);
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Parse vertex line (v)
        if (strncmp(line, "v ", 2) == 0) {
            // Resize the vertex array if capacity is exceeded
            if (mesh->vertex_count >= vertex_capacity) {
                vertex_capacity *= 2;
                mesh->vertices = (Vertex *)realloc(mesh->vertices, vertex_capacity * sizeof(Vertex));
                if (!mesh->vertices) {
                    fprintf(stderr, "Failed to reallocate memory for vertices.\n");
                    fclose(file);
                    return -1;
                }
            }

            // Read vertex coordinates
            sscanf(line + 2, "%f %f %f", &mesh->vertices[mesh->vertex_count].x,
                   &mesh->vertices[mesh->vertex_count].y, &mesh->vertices[mesh->vertex_count].z);
            mesh->vertex_count++;
        }
        // Detect and skip vertex normal line (vn)
        else if (strncmp(line, "vn ", 3) == 0) {
            continue;
        }
        // Detect and skip texture coordinate line (vt)
        else if (strncmp(line, "vt ", 3) == 0) {
            continue;
        }
        // Parse face (f) line
        else if (strncmp(line, "f ", 2) == 0) {
            // Resize the triangle array if capacity is exceeded
            if (mesh->triangle_count >= triangle_capacity) {
                triangle_capacity *= 2;
                mesh->triangles = (Triangle *)realloc(mesh->triangles, triangle_capacity * sizeof(Triangle));
                if (!mesh->triangles) {
                    fprintf(stderr, "Failed to reallocate memory for triangles.\n");
                    fclose(file);
                    return -1;
                }
            }

            // Parse face with vertex indices (ignoring texture and normal indices)
            int v1, v2, v3;
            sscanf(line + 2, "%d/%*d/%*d %d/%*d/%*d %d/%*d/%*d", &v1, &v2, &v3);
            mesh->triangles[mesh->triangle_count].v1 = v1 - 1;  // Convert to 0-based indexing
            mesh->triangles[mesh->triangle_count].v2 = v2 - 1;
            mesh->triangles[mesh->triangle_count].v3 = v3 - 1;
            mesh->triangle_count++;
        }
    }

    fclose(file);
    return 0;
}

int get_triangle_mesh(const char *id, TriangleMesh *mesh) {
    char *obj_file_path = NULL;
    if (fetch_obj_file(id, &obj_file_path) != 0) {
        fprintf(stderr, "Failed to fetch .obj file for ID: %s\n", id);
        return -1;
    }

    if (parse_obj_file(obj_file_path, mesh) != 0) {
        fprintf(stderr, "Failed to parse .obj file: %s\n", obj_file_path);
        free(obj_file_path);
        return -1;
    }

    free(obj_file_path);
    return 0;
}

int write_trianglemesh_to_obj(const char *filename, const TriangleMesh *mesh) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Error: Could not open file for writing: %s\n", filename);
        return -1;
    }

    // Write vertices
    for (size_t i = 0; i < mesh->vertex_count; ++i) {
        const Vertex *v = &mesh->vertices[i];
        fprintf(file, "v %f %f %f\n", v->x, v->y, v->z);
    }

    // Write faces (triangles)
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        const Triangle *t = &mesh->triangles[i];
        // OBJ uses 1-based indexing, so increment the vertex indices by 1
        fprintf(file, "f %d %d %d\n", t->v1 + 1, t->v2 + 1, t->v3 + 1);
    }

    fclose(file);
    return 0;
}

RegionOfInterest get_mesh_bounding_box(const TriangleMesh *mesh) {
    // Initialize the bounding box to extreme values
    float min_x = FLT_MAX, min_y = FLT_MAX, min_z = FLT_MAX;
    float max_x = -FLT_MAX, max_y = -FLT_MAX, max_z = -FLT_MAX;

    // Traverse through all vertices to find the bounding box
    for (size_t i = 0; i < mesh->vertex_count; ++i) {
        const Vertex *v = &mesh->vertices[i];

        if (v->x < min_x) min_x = v->x;
        if (v->y < min_y) min_y = v->y;
        if (v->z < min_z) min_z = v->z;

        if (v->x > max_x) max_x = v->x;
        if (v->y > max_y) max_y = v->y;
        if (v->z > max_z) max_z = v->z;
    }

    // Calculate the dimensions of the bounding box
    RegionOfInterest roi;
    roi.x_start = min_x;
    roi.y_start = min_y;
    roi.z_start = min_z;
    roi.x_width = max_x - min_x;
    roi.y_height = max_y - min_y;
    roi.z_depth = max_z - min_z;

    return roi;
}

void reset_mesh_origin_to_roi(TriangleMesh *mesh, const RegionOfInterest *roi) {
    // Subtract the ROI origin from each vertex in the mesh
    for (size_t i = 0; i < mesh->vertex_count; ++i) {
        mesh->vertices[i].x -= roi->x_start;
        mesh->vertices[i].y -= roi->y_start;
        mesh->vertices[i].z -= roi->z_start;
    }
}


//vesuvius notes:
// - when passing pointers to a _new function in order to fill out fields in the struct (e.g. vs_mesh_new)
//   the struct will take ownership of the pointer and the pointer shall be cleaned up in the _free function.
//   The caller loses ownership of the pointer
// - index order is in Z Y X order
// - a 0 return code indicates success for functions that do NOT return a pointer
// - a non zero return code indicates failure
// - a NULL pointer indicates failure for functions that return a pointer

// in order to use Vesuvius-c, define VESUVIUS_IMPL in one .c file and then #include "vesuvius-c.h" 
// in order to use curl, which depends on libcurl and ssl, define VESUVIUS_CURL_IMPL 
// in order to use zarr, which depends on cblosc2 and json.h, define VESUVIUS_ZARR_IMPL

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>
#include <float.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#ifdef VESUVIUS_CURL_IMPL
#include <curl/curl.h>
#endif

#ifdef VESUVIUS_ZARR_IMPL
#include <blosc2.h>
#include <json.h>
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef float f32;
typedef double f64;


typedef struct histogram {
    s32 num_bins;
    f32 min_value;
    f32 max_value;
    f32 bin_width;
    u32 *bins;
} histogram;

typedef struct hist_stats {
    f32 mean;
    f32 median;
    f32 mode;
    u32 mode_count;
    f32 std_dev;
} hist_stats;

#ifdef VESUVIUS_CURL_IMPL
typedef struct {
    char* buffer;
    size_t size;
} DownloadBuffer;
#endif

typedef struct chunk {
    int dims[3];
    float data[];
} chunk __attribute__((aligned(16)));

typedef struct slice {
    int dims[2];
    float data[];
} slice __attribute__((aligned(16)));

// meshes are triangle only. every 3 entries in vertices corresponds to a new vertex
// normals are 3 component
typedef struct {
    f32 *vertices; // cannot be null
    s32 *indices; // cannot be null
    f32 *normals; // can be null if no normals
    s32 vertex_count;
    s32 index_count;
} mesh;

#define MAX_LINE_LENGTH 1024
#define MAX_HEADER_LINES 100

typedef struct {
    char type[32];
    s32 dimension;
    char space[32];
    s32 sizes[16];
    f32 space_directions[16][3];
    char endian[16];
    char encoding[32];
    f32 space_origin[3];

    size_t data_size;
    void* data;

    bool is_valid;
} nrrd;


// PPM format types
typedef enum ppm_type {
    P3,  // ASCII format
    P6   // Binary format
} ppm_type;

typedef struct ppm {
    u32 width;
    u32 height;
    u8 max_val;
    u8* data;  // RGB data in row-major order
} ppm;

// tiff
#define TIFFTAG_SUBFILETYPE 254
#define TIFFTAG_IMAGEWIDTH 256
#define TIFFTAG_IMAGELENGTH 257
#define TIFFTAG_BITSPERSAMPLE 258
#define TIFFTAG_COMPRESSION 259
#define TIFFTAG_PHOTOMETRIC 262
#define TIFFTAG_IMAGEDESCRIPTION 270
#define TIFFTAG_SOFTWARE 305
#define TIFFTAG_DATETIME 306
#define TIFFTAG_SAMPLESPERPIXEL 277
#define TIFFTAG_ROWSPERSTRIP 278
#define TIFFTAG_PLANARCONFIG 284
#define TIFFTAG_RESOLUTIONUNIT 296
#define TIFFTAG_XRESOLUTION 282
#define TIFFTAG_YRESOLUTION 283
#define TIFFTAG_SAMPLEFORMAT 339
#define TIFFTAG_STRIPOFFSETS 273
#define TIFFTAG_STRIPBYTECOUNTS 279

#define TIFF_BYTE 1
#define TIFF_ASCII 2
#define TIFF_SHORT 3
#define TIFF_LONG 4
#define TIFF_RATIONAL 5

typedef struct {
    uint32_t offset;
    uint32_t byteCount;
} StripInfo;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint16_t bitsPerSample;
    uint16_t compression;
    uint16_t photometric;
    uint16_t samplesPerPixel;
    uint32_t rowsPerStrip;
    uint16_t planarConfig;
    uint16_t sampleFormat;
    StripInfo stripInfo;
    char imageDescription[256];
    char software[256];
    char dateTime[20];
    float xResolution;
    float yResolution;
    uint16_t resolutionUnit;
    uint32_t subfileType;
} DirectoryInfo;

typedef struct {
    DirectoryInfo* directories;
    uint16_t depth;
    size_t dataSize;
    void* data;
    bool isValid;
    char errorMsg[256];
} TiffImage;

// vol
// A volume is an entire scroll
//   - for Scroll 1 it is all 14376 x 7888 x 8096 voxels
//   - the dtype is uint8 or uint16
//

typedef struct volume {
    s32 dims[3];
    bool is_zarr;
    bool is_tif_stack;
    bool uses_3d_tif;
    char* cache_dir;
    u64 vol_id;
} volume;

//zarr
#ifdef VESUVIUS_ZARR_IMPL
typedef struct zarr_compressor_settings {
    int32_t blocksize;
    int32_t clevel;
    char cname[32];
    char id[32];
    int32_t shuffle;
} zarr_compressor_settings;

typedef struct zarr_metadata {
    int32_t shape[3];
    int32_t chunks[3];
    zarr_compressor_settings compressor;
    char dtype[8];
    int32_t fill_value;
    char order; // Single character 'C' or 'F'
    int32_t zarr_format;
} zarr_metadata;
#endif

// Public APIs
// - These are exported and meant to be used by users of vesuvius-c.h

// chamfer
f32 vs_chamfer_distance(const f32* set1, s32 size1, const f32* set2, s32 size2);

// curl
long vs_download(const char* url, void** out_buffer);

// histogram
histogram *vs_histogram_new(s32 num_bins, f32 min_value, f32 max_value);
void vs_histogram_free(histogram *hist);
histogram* vs_slice_histogram(const f32* data, s32 dimy, s32 dimx, s32 num_bins);
histogram* vs_chunk_histogram(const f32* data, s32 dimz, s32 dimy, s32 dimx, s32 num_bins);
s32 vs_write_histogram_to_csv(const histogram *hist, const char *filename);
hist_stats vs_calculate_histogram_stats(const histogram *hist);

// math
chunk *vs_chunk_new(int dims[static 3]);
void vs_chunk_free(chunk *chunk);
slice *vs_slice_new(int dims[static 2]);
void vs_slice_free(slice *slice);
f32 vs_slice_get(slice *slice, s32 y, s32 x);
void vs_slice_set(slice *slice, s32 y, s32 x, f32 data);
f32 vs_chunk_get(chunk *chunk, s32 z, s32 y, s32 x);
void vs_chunk_set(chunk *chunk, s32 z, s32 y, s32 x, f32 data);
chunk* vs_maxpool(chunk* inchunk, s32 kernel, s32 stride);
chunk *vs_avgpool(chunk *inchunk, s32 kernel, s32 stride);
chunk *vs_sumpool(chunk *inchunk, s32 kernel, s32 stride);
chunk* vs_unsharp_mask_3d(chunk* input, float amount, s32 kernel_size);
chunk* vs_normalize_chunk(chunk* input);
chunk* vs_transpose(chunk* input, const char* current_layout);

// mesh
mesh* vs_mesh_new(f32 *vertices, f32 *normals, s32 *indices, s32 vertex_count, s32 index_count);
void vs_mesh_free(mesh *mesh);
void vs_mesh_get_bounds(const mesh *m,
                    f32 *origin_z, f32 *origin_y, f32 *origin_x,
                    f32 *length_z, f32 *length_y, f32 *length_x);
void vs_mesh_translate(mesh *m, f32 z, f32 y, f32 x);
void vs_mesh_scale(mesh *m, f32 scale_z, f32 scale_y, f32 scale_x);
s32 vs_march_cubes(const f32* values,
                s32 dimz, s32 dimy, s32 dimx,
                f32 isovalue,
                f32** out_vertices,      //  [z,y,x,z,y,x,...]
                s32** out_indices,
                s32* out_vertex_count,
                s32* out_index_count);

// nrrd
nrrd* vs_nrrd_read(const char* filename);
void vs_nrrd_free(nrrd* nrrd);

// obj
s32 vs_read_obj(const char* filename,
            f32** vertices, s32** indices,
            s32* vertex_count, s32* index_count);
s32 vs_write_obj(const char* filename,
             const f32* vertices, const s32* indices,
             s32 vertex_count, s32 index_count);

// ply
s32 vs_ply_write(const char *filename,
                    const f32 *vertices,
                    const f32 *normals, // can be NULL if no normals
                    const s32 *indices,
                    s32 vertex_count,
                    s32 index_count);
s32 vs_ply_read(const char *filename,
                          f32 **out_vertices,
                          f32 **out_normals,
                          s32 **out_indices,
                          s32 *out_vertex_count,
                          s32 *out_normal_count,
                          s32 *out_index_count);

//ppm
ppm* vs_ppm_new(u32 width, u32 height);
inline void vs_ppm_free(ppm* img);
ppm* vs_ppm_read(const char* filename);
int vs_ppm_write(const char* filename, const ppm* img, ppm_type type);
void vs_ppm_set_pixel(ppm* img, u32 x, u32 y, u8 r, u8 g, u8 b);
void vs_ppm_get_pixel(const ppm* img, u32 x, u32 y, u8* r, u8* g, u8* b);

//tiff
TiffImage* vs_tiff_read(const char* filename);
void vs_tiff_free(TiffImage* img);
const char* vs_tiff_compression_name(uint16_t compression);
const char* vs_tiff_photometric_name(uint16_t photometric);
const char* vs_tiff_planar_config_name(uint16_t config);
const char* vs_tiff_sample_format_name(uint16_t format);
const char* vs_tiff_resolution_unit_name(uint16_t unit);
void vs_tiff_print_tags(const TiffImage* img, int directory);
void vs_tiff_print_all_tags(const TiffImage* img);
size_t vs_tiff_directory_size(const TiffImage* img, int directory);
void* vs_tiff_read_directory_data(const TiffImage* img, int directory);
uint16_t vs_tiff_pixel16(const uint16_t* buffer, int y, int x, int width);
uint8_t vs_tiff_pixel8(const uint8_t* buffer, int y, int x, int width);
int vs_tiff_write(const char* filename, const TiffImage* img, bool littleEndian);
TiffImage* vs_tiff_create(uint32_t width, uint32_t height, uint16_t depth, uint16_t bitsPerSample);

// vcps
int vs_vcps_read(const char* filename,
              size_t* width, size_t* height, size_t* dim,
              void* data, const char* dst_type);
int vs_vcps_write(const char* filename,
               size_t width, size_t height, size_t dim,
               const void* data, const char* src_type, const char* dst_type);

// volume
volume *vs_vol_new(s32 dims[static 3], bool is_zarr, bool is_tif_stack, bool uses_3d_tif, char* cache_dir, u64 vol_id);
chunk* vs_vol_get_chunk(volume* vol, s32 chunk_pos[static 3], s32 chunk_dims[static 3]);

// zarr
#ifdef VESUVIUS_ZARR_IMPL
zarr_metadata vs_zarr_parse_zarray(char *path);
#endif

// vesuvius specific
chunk *vs_tiff_to_chunk(const char *tiffpath);
slice *vs_tiff_to_slice(const char *tiffpath, int index);
int vs_slice_fill(slice *slice, volume *vol, int start[static 2], int axis);
int vs_chunk_fill(chunk *chunk, volume *vol, int start[static 3]);

#ifdef VESUVIUS_IMPL

// Private APIs
// - These are not exported and are only meant to be used within vesuvius-c.h itself

// utils
static void vs__trim(char* str);
static void vs__skip_line(FILE *fp);
static bool vs__str_starts_with(const char* str, const char* prefix);
static int vs__mkdir_p(const char* path);
static bool vs__path_exists(const char *path);

//chamfer
static f32 vs__squared_distance(const f32* p1, const f32* p2);
static f32 vs__min_distance_to_set(const f32* point, const f32* point_set, s32 set_size);

//curl
static size_t vs__write_callback(void *contents, size_t size, size_t nmemb, void *userp);

//histogram
static s32 vs__get_bin_index(const histogram* hist, f32 value);
static f32 vs__get_slice_value(const f32* data, s32 y, s32 x, s32 dimx);
static f32 vs__get_chunk_value(const f32* data, s32 z, s32 y, s32 x, s32 dimy, s32 dimx);

// math
static float vs__maxfloat(float a, float b);
static float vs__minfloat(float a, float b);
static float vs__avgfloat(float *data, int len);
static chunk *vs__create_box_kernel(s32 size);
static chunk* vs__convolve3d(chunk* input, chunk* kernel);

// mesh
static void vs__interpolate_vertex(f32 isovalue,
                                    f32 v1, f32 v2,
                                    f32 x1, f32 y1, f32 z1,
                                    f32 x2, f32 y2, f32 z2,
                                    f32* out_x, f32* out_y, f32* out_z);
static void vs__process_cube(const f32* values,
                        s32 x, s32 y, s32 z,
                        s32 dimx, s32 dimy, s32 dimz,
                        f32 isovalue,
                        f32* vertices,
                        s32* indices,
                        s32* vertex_count,
                        s32* index_count);
static f32 vs__get_value(const f32* values, s32 x, s32 y, s32 z, s32 dimx, s32 dimy, s32 dimz);

//nrrd
static int vs__nrrd_parse_sizes(char* value, nrrd* nrrd);
static int vs__nrrd_parse_space_directions(char* value, nrrd* nrrd);
static int vs__nrrd_parse_space_origin(char* value, nrrd* nrrd);
static size_t vs__nrrd_get_type_size(const char* type);
static int vs__nrrd_read_raw_data(FILE* fp, nrrd* nrrd);
static int vs__nrrd_read_gzip_data(FILE* fp, nrrd* nrrd);

//ppm
static void vs__skip_whitespace_and_comments(FILE* fp);
static bool vs__ppm_read_header(FILE* fp, ppm_type* type, u32* width, u32* height, u8* max_val);

//tiff
static uint32_t vs__tiff_read_bytes(FILE* fp, int count, int littleEndian);
static void vs__tiff_read_string(FILE* fp, char* str, uint32_t offset, uint32_t count, long currentPos);
static float vs__tiff_read_rational(FILE* fp, uint32_t offset, int littleEndian, long currentPos);
static void vs__tiff_read_ifd_entry(FILE* fp, DirectoryInfo* dir, int littleEndian, long ifdStart);
static bool vs__tiff_validate_directory(DirectoryInfo* dir, TiffImage* img);
static void vs__tiff_write_bytes(FILE* fp, uint32_t value, int count, int littleEndian);
static void vs__tiff_write_string(FILE* fp, const char* str, uint32_t offset);
static void vs__tiff_write_rational(FILE* fp, float value, uint32_t offset, int littleEndian);
static void vs__tiff_current_date_time(char* dateTime);
static uint32_t vs__tiff_write_ifd_entry(FILE* fp, uint16_t tag, uint16_t type, uint32_t count, uint32_t value, int littleEndian);

//vcps
static int vs__vcps_read_binary_data(FILE* fp, void* out_data, const char* src_type, const char* dst_type, size_t count);
static int vs__vcps_write_binary_data(FILE* fp, const void* data, const char* src_type, const char* dst_type, size_t count);

//zarr
#ifdef VESUVIUS_ZARR_IMPL
static struct json_value_s *vs__json_find_value(const struct json_object_s *obj, const char *key);
static void vs__json_parse_int32_array(struct json_array_s *array, int32_t output[3]);
static int vs__zarr_parse_metadata(const char *json_string, zarr_metadata *metadata);
#endif


static void vs__trim(char* str) {
  char* end;
  while(isspace(*str)) str++;
  if(*str == 0) return;
  end = str + strlen(str) - 1;
  while(end > str && isspace(*end)) end--;
  end[1] = '\0';
}

static bool vs__str_starts_with(const char* str, const char* prefix) {
  return strncmp(str, prefix, strlen(prefix)) == 0;
}

static int vs__mkdir_p(const char* path) {
  char tmp[1024];
  char* p = NULL;
  size_t len;

  snprintf(tmp, sizeof(tmp), "%s", path);
  len = strlen(tmp);
  if (tmp[len - 1] == '/') {
    tmp[len - 1] = 0;
  }

  for (p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
#ifdef _WIN32
      mkdir(tmp);
#else
      mkdir(tmp, 0755);
#endif
      *p = '/';
    }
  }

#ifdef _WIN32
  return (mkdir(tmp) == 0 || errno == EEXIST) ? 0 : 1;
#else
  return (mkdir(tmp, 0755) == 0 || errno == EEXIST) ? 0 : 1;
#endif
}

static bool vs__path_exists(const char *path) {
    return access(path, F_OK) == 0 ? true : false;
}

// chamfer

static f32 vs__squared_distance(const f32* p1, const f32* p2) {
  f32 dz = p1[0] - p2[0];
  f32 dy = p1[1] - p2[1];
  f32 dx = p1[2] - p2[2];
  return dx*dx + dy*dy + dz*dz;
}


static f32 vs__min_distance_to_set(const f32* point, const f32* point_set, s32 set_size) {
  f32 min_dist = FLT_MAX;

  for (s32 i = 0; i < set_size; i++) {
    f32 dist = vs__squared_distance(point, &point_set[i * 3]);
    if (dist < min_dist) {
      min_dist = dist;
    }
  }
  return min_dist;
}

f32 vs_chamfer_distance(const f32* set1, s32 size1, const f32* set2, s32 size2) {
  f32 sum1 = 0.0f;
  f32 sum2 = 0.0f;

  for (s32 i = 0; i < size1; i++) {
    sum1 += vs__min_distance_to_set(&set1[i * 3], set2, size2);
  }

  for (s32 i = 0; i < size2; i++) {
    sum2 += vs__min_distance_to_set(&set2[i * 3], set1, size1);
  }

  return sqrtf((sum1 / size1 + sum2 / size2) / 2.0f);
}

//curl
#ifdef VESUVIUS_CURL_IMPL
static size_t vs__write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    DownloadBuffer *mem = userp;

    char *new_buffer = realloc(mem->buffer, mem->size + realsize + 1);  // +1 for null terminator
    if (!new_buffer) {
        return 0; // Signal error to curl
    }

    memcpy(new_buffer + mem->size, contents, realsize);
    mem->buffer = new_buffer;
    mem->size += realsize;
    mem->buffer[mem->size] = 0; // Null terminate

    return realsize;
}

long vs_download(const char* url, void** out_buffer) {
    CURL* curl;
    CURLcode res;
    long http_code = 0;

    DownloadBuffer chunk = {
        .buffer = malloc(1),
        .size = 0
    };

    if (!chunk.buffer) {
        return -1;
    }
    chunk.buffer[0] = 0;  // Ensure null terminated

    curl = curl_easy_init();
    if (!curl) {
        free(chunk.buffer);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, vs__write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    //TODO: with bearssl on windows I have to disable these
    // does that matter?
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        free(chunk.buffer);
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        free(chunk.buffer);
        return -1;
    }

    *out_buffer = chunk.buffer;
    return chunk.size;
}
#else
long vs_download(const char* url, void** out_buffer) {
    printf("curl support must be enabled to download files\n");
    return -1;
}
#endif

// histogram
histogram *vs_histogram_new(s32 num_bins, f32 min_value, f32 max_value) {
  histogram *hist = malloc(sizeof(histogram));
  if (!hist) {
    return NULL;
  }

  hist->bins = calloc(num_bins, sizeof(u32));
  if (!hist->bins) {
    free(hist);
    return NULL;
  }

  hist->num_bins = num_bins;
  hist->min_value = min_value;
  hist->max_value = max_value;
  hist->bin_width = (max_value - min_value) / num_bins;

  return hist;
}

void vs_histogram_free(histogram *hist) {
  if (hist) {
    free(hist->bins);
    free(hist);
  }
}


static s32 vs__get_bin_index(const histogram* hist, f32 value) {
    if (value <= hist->min_value) return 0;
    if (value >= hist->max_value) return hist->num_bins - 1;

    s32 bin = (s32)((value - hist->min_value) / hist->bin_width);
    if (bin >= hist->num_bins) bin = hist->num_bins - 1;
    return bin;
}

histogram* vs_slice_histogram(const f32* data,
                                      s32 dimy, s32 dimx,
                                      s32 num_bins) {
    if (!data || num_bins <= 0) {
        return NULL;
    }

    f32 min_val = FLT_MAX;
    f32 max_val = -FLT_MAX;

    s32 total_pixels = dimy * dimx;
    for (s32 i = 0; i < total_pixels; i++) {
        f32 val = data[i];
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
    }

    histogram* hist = vs_histogram_new(num_bins, min_val, max_val);
    if (!hist) {
        return NULL;
    }

    for (s32 i = 0; i < total_pixels; i++) {
        s32 bin = vs__get_bin_index(hist, data[i]);
        hist->bins[bin]++;
    }

    return hist;
}

histogram* vs_chunk_histogram(const f32* data,
                                      s32 dimz, s32 dimy, s32 dimx,
                                      s32 num_bins) {
    if (!data || num_bins <= 0) {
        return NULL;
    }

    f32 min_val = FLT_MAX;
    f32 max_val = -FLT_MAX;

    s32 total_voxels = dimz * dimy * dimx;
    for (s32 i = 0; i < total_voxels; i++) {
        f32 val = data[i];
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
    }

    histogram* hist = vs_histogram_new(num_bins, min_val, max_val);
    if (!hist) {
        return NULL;
    }

    for (s32 i = 0; i < total_voxels; i++) {
        s32 bin = vs__get_bin_index(hist, data[i]);
        hist->bins[bin]++;
    }

    return hist;
}

static f32 vs__get_slice_value(const f32* data, s32 y, s32 x, s32 dimx) {
    return data[y * dimx + x];
}

static f32 vs__get_chunk_value(const f32* data, s32 z, s32 y, s32 x,
                                  s32 dimy, s32 dimx) {
    return data[z * (dimy * dimx) + y * dimx + x];
}
s32 vs_write_histogram_to_csv(const histogram *hist, const char *filename) {
  FILE *fp = fopen(filename, "w");
  if (!fp) {
    return 1;
  }

  fprintf(fp, "bin_start,bin_end,count\n");

  for (s32 i = 0; i < hist->num_bins; i++) {
    f32 bin_start = hist->min_value + i * hist->bin_width;
    f32 bin_end = bin_start + hist->bin_width;
    fprintf(fp, "%.6f,%.6f,%u\n", bin_start, bin_end, hist->bins[i]);
  }

  fclose(fp);
  return 0;
}

hist_stats vs_calculate_histogram_stats(const histogram *hist) {
  hist_stats stats = {0};

  unsigned long long total_count = 0;
  f64 weighted_sum = 0.0;
  u32 max_count = 0;

  for (s32 i = 0; i < hist->num_bins; i++) {
    f32 bin_center = hist->min_value + (i + 0.5f) * hist->bin_width;
    weighted_sum += bin_center * hist->bins[i];
    total_count += hist->bins[i];

    if (hist->bins[i] > max_count) {
      max_count = hist->bins[i];
      stats.mode = bin_center;
      stats.mode_count = hist->bins[i];
    }
  }

  stats.mean = (f32) (weighted_sum / total_count);

  f64 variance_sum = 0;
  for (s32 i = 0; i < hist->num_bins; i++) {
    f32 bin_center = hist->min_value + (i + 0.5f) * hist->bin_width;
    f32 diff = bin_center - stats.mean;
    variance_sum += diff * diff * hist->bins[i];
  }
  stats.std_dev = (f32) sqrt(variance_sum / total_count);

  u64 median_count = total_count / 2;
  u64 running_count = 0;
  for (s32 i = 0; i < hist->num_bins; i++) {
    running_count += hist->bins[i];
    if (running_count >= median_count) {
      stats.median = hist->min_value + (i + 0.5f) * hist->bin_width;
      break;
    }
  }

  return stats;
}

// math


// A chunk is a 3d cross section of data
//   - this could be a 512x512x512 section starting at 2000x2000x2000 and ending at 2512 x 2512 x 2512
//   - the dtype is float32
//   - increasing Z means increasing through the slice. e.g. 1000.tif -> 1001.tif
//   - increasing Y means looking farther down in a slice
//   - increasing X means looking farther right in a slice
// A slice is a 2d cross section of data
//   - increasing Y means looking farther down in a slice
//   - increasing X means looking farther right in a slice


static float vs__maxfloat(float a, float b) { return a > b ? a : b; }
static float vs__minfloat(float a, float b) { return a < b ? a : b; }
static float vs__avgfloat(float *data, int len) {
  double sum = 0.0;
  for (int i = 0; i < len; i++) sum += data[i];
  return sum / len;
}

chunk *vs_chunk_new(int dims[static 3]) {
  chunk *ret = malloc(sizeof(chunk) + dims[0] * dims[1] * dims[2] * sizeof(float));

  if (ret == NULL) {
    assert(false);
    return NULL;
  }

  for (int i = 0; i < 3; i++) {
    ret->dims[i] = dims[i];
  }
  return ret;
}

void vs_chunk_free(chunk *chunk) {
  free(chunk);
}

slice *vs_slice_new(int dims[static 2]) {
  slice *ret = malloc(sizeof(slice) + dims[0] * dims[1] * sizeof(float));

  if (ret == NULL) {
    assert(false);
    return NULL;
  }

  for (int i = 0; i < 2; i++) {
    ret->dims[i] = dims[i];
  }
  return ret;
}

void vs_slice_free(slice *slice) {
  free(slice);
}

f32 vs_slice_get(slice *slice, s32 y, s32 x) {
  return slice->data[y * slice->dims[1] + x];
}

void vs_slice_set(slice *slice, s32 y, s32 x, f32 data) {
  slice->data[y * slice->dims[1] + x] = data;
}

f32 vs_chunk_get(chunk *chunk, s32 z, s32 y, s32 x) {
  return chunk->data[z * chunk->dims[1] * chunk->dims[2] + y * chunk->dims[2] + x];
}

void vs_chunk_set(chunk *chunk, s32 z, s32 y, s32 x, f32 data) {
  chunk->data[z * chunk->dims[1] * chunk->dims[2] + y * chunk->dims[2] + x] = data;
}


chunk* vs_maxpool(chunk* inchunk, s32 kernel, s32 stride) {
  s32 dims[3] = {
    (inchunk->dims[0] + stride - 1) / stride, (inchunk->dims[1] + stride - 1) / stride,
    (inchunk->dims[2] + stride - 1) / stride
  };
  chunk *ret = vs_chunk_new(dims);
  for (s32 z = 0; z < ret->dims[0]; z++)
    for (s32 y = 0; y < ret->dims[1]; y++)
      for (s32 x = 0; x < ret->dims[2]; x++) {
        f32 max32 = -INFINITY;
        f32 val32;
        for (s32 zi = 0; zi < kernel; zi++)
          for (s32 yi = 0; yi < kernel; yi++)
            for (s32 xi = 0; xi < kernel; xi++) {
              if (z + zi > inchunk->dims[0] || y + yi > inchunk->dims[1] || x + xi > inchunk->dims[2]) { continue; }

              if ((val32 = vs_chunk_get(inchunk, z * stride + zi, y * stride + yi,
                                                                       x * stride + xi)) > max32) { max32 = val32; }
            }
        vs_chunk_set(ret, z, y, x, max32);
      }
  return ret;
}

chunk *vs_avgpool(chunk *inchunk, s32 kernel, s32 stride) {
  s32 dims[3] = {
    (inchunk->dims[0] + stride - 1) / stride, (inchunk->dims[1] + stride - 1) / stride,
    (inchunk->dims[2] + stride - 1) / stride
  };
  chunk *ret = vs_chunk_new(dims);
  s32 len = kernel * kernel * kernel;
  s32 i = 0;
  f32 *data = malloc(len * sizeof(f32));
  for (s32 z = 0; z < ret->dims[0]; z++)
    for (s32 y = 0; y < ret->dims[1]; y++)
      for (s32 x = 0; x < ret->dims[2]; x++) {
        len = kernel * kernel * kernel;
        i = 0;
        for (s32 zi = 0; zi < kernel; zi++)
          for (s32 yi = 0; yi < kernel; yi++)
            for (s32 xi = 0; xi < kernel; xi++) {
              if (z + zi > inchunk->dims[0] || y + yi > inchunk->dims[1] || x + xi > inchunk->dims[2]) {
                len--;
                continue;
              }
              data[i++] = vs_chunk_get(inchunk, z * stride + zi, y * stride + yi, x * stride + xi);
            }
        vs_chunk_set(ret, z, y, x, vs__avgfloat(data, len));
      }
  return ret;
}

chunk *vs_sumpool(chunk *inchunk, s32 kernel, s32 stride) {
  s32 dims[3] = {
    (inchunk->dims[0] + stride - 1) / stride, (inchunk->dims[1] + stride - 1) / stride,
    (inchunk->dims[2] + stride - 1) / stride
  };
  chunk *ret = vs_chunk_new(dims);
  for (s32 z = 0; z < ret->dims[0]; z++)
    for (s32 y = 0; y < ret->dims[1]; y++)
      for (s32 x = 0; x < ret->dims[2]; x++) {
        f32 sum = 0.0f;
        for (s32 zi = 0; zi < kernel; zi++)
          for (s32 yi = 0; yi < kernel; yi++)
            for (s32 xi = 0; xi < kernel; xi++) {
              if (z + zi > inchunk->dims[0] || y + yi > inchunk->dims[1] || x + xi > inchunk->dims[2]) {
                continue;
              }
              sum += vs_chunk_get(inchunk, z * stride + zi, y * stride + yi, x * stride + xi);
            }
        vs_chunk_set(ret, z, y, x, sum);
      }
  return ret;
}


static chunk *vs__create_box_kernel(s32 size) {
  int dims[3] = {size,size,size};
  chunk* kernel = vs_chunk_new(dims);
  float value = 1.0f / (size * size * size);
  for (s32 z = 0; z < size; z++) {
    for (s32 y = 0; y < size; y++) { for (s32 x = 0; x < size; x++) { vs_chunk_set(kernel, z, y, x, value); } }
  }
  return kernel;
}

static chunk* vs__convolve3d(chunk* input, chunk* kernel) {

  s32 dims[3] = {input->dims[0], input->dims[1], input->dims[2]};

  chunk* ret = vs_chunk_new(dims);
  s32 pad = kernel->dims[0] / 2;

  for (s32 z = 0; z < input->dims[0]; z++) {
    for (s32 y = 0; y < input->dims[1]; y++) {
      for (s32 x = 0; x < input->dims[2]; x++) {
        float sum = 0.0f;
        for (s32 kz = 0; kz < kernel->dims[0]; kz++) {
          for (s32 ky = 0; ky < kernel->dims[1]; ky++) {
            for (s32 kx = 0; kx < kernel->dims[2]; kx++) {
              s32 iz = z + kz - pad;
              s32 iy = y + ky - pad;
              s32 ix = x + kx - pad;
              if (iz >= 0 && iz < input->dims[0] && iy >= 0 && iy < input->dims[1] && ix >= 0 && ix < input->dims[2]) {
                float input_val = vs_chunk_get(input, iz, iy, ix);
                sum += input_val * vs_chunk_get(kernel, kz, ky, kx);
              }
            }
          }
        }
        vs_chunk_set(ret, z, y, x, sum);
      }
    }
  }
  return ret;
}

chunk* vs_unsharp_mask_3d(chunk* input, float amount, s32 kernel_size) {
  int dims[3] = {input->dims[0], input->dims[1], input->dims[2]};
  chunk* kernel = vs__create_box_kernel(kernel_size);
  chunk* blurred = vs__convolve3d(input, kernel);
  chunk* output = vs_chunk_new(dims);

  for (s32 z = 0; z < input->dims[0]; z++) {
    for (s32 y = 0; y < input->dims[1]; y++) {
      for (s32 x = 0; x < input->dims[2]; x++) {
        float original = vs_chunk_get(input, z, y, x);
        float blur = vs_chunk_get(blurred, z, y, x);
        float sharpened = original + amount * (original - blur);
        vs_chunk_set(output, z, y, x, sharpened);
      }
    }
  }

  vs_chunk_free(kernel);
  vs_chunk_free(blurred);

  return output;
}

chunk* vs_normalize_chunk(chunk* input) {
  // Create output chunk with same dimensions
  int dims[3] = {input->dims[0], input->dims[1], input->dims[2]};
  chunk* output = vs_chunk_new(dims);

  // First pass: find min and max values
  float min_val = INFINITY;
  float max_val = -INFINITY;

  for (s32 z = 0; z < input->dims[0]; z++) {
    for (s32 y = 0; y < input->dims[1]; y++) {
      for (s32 x = 0; x < input->dims[2]; x++) {
        float val = vs_chunk_get(input, z, y, x);
        min_val = vs__minfloat(min_val, val);
        max_val = vs__maxfloat(max_val, val);
      }
    }
  }

  // Handle edge case where all values are the same
  float range = max_val - min_val;
  if (range == 0.0f) {
    for (s32 z = 0; z < input->dims[0]; z++) {
      for (s32 y = 0; y < input->dims[1]; y++) {
        for (s32 x = 0; x < input->dims[2]; x++) {
          vs_chunk_set(output, z, y, x, 0.5f);
        }
      }
    }
    return output;
  }

  // Second pass: normalize values to [0.0, 1.0]
  for (s32 z = 0; z < input->dims[0]; z++) {
    for (s32 y = 0; y < input->dims[1]; y++) {
      for (s32 x = 0; x < input->dims[2]; x++) {
        float val = vs_chunk_get(input, z, y, x);
        float normalized = (val - min_val) / range;
        vs_chunk_set(output, z, y, x, normalized);
      }
    }
  }

  return output;
}

chunk* vs_transpose(chunk* input, const char* current_layout) {
    if (!input || !current_layout || strlen(current_layout) != 3) {
        return NULL;
    }

    int mapping[3] = {0, 0, 0};

    for (int i = 0; i < 3; i++) {
        switch (current_layout[i]) {
            case 'z':
                mapping[0] = i;
                break;
            case 'y':
                mapping[1] = i;
                break;
            case 'x':
                mapping[2] = i;
                break;
            default:
                return NULL;
        }
    }

    int new_dims[3] = {
        input->dims[mapping[0]],  // z
        input->dims[mapping[1]],  // y
        input->dims[mapping[2]]   // x
    };

    chunk* output = vs_chunk_new(new_dims);
    if (!output) {
        return NULL;
    }

    // Perform the vs_transpose
    for (int z = 0; z < new_dims[0]; z++) {
        for (int y = 0; y < new_dims[1]; y++) {
            for (int x = 0; x < new_dims[2]; x++) {
                int idx[3] = {z, y, x};

                int old_indices[3] = {
                    idx[mapping[0]],  // Z
                    idx[mapping[1]],  // Y
                    idx[mapping[2]]   // C
                };

                float value = vs_chunk_get(input, old_indices[0], old_indices[1], old_indices[2]);
                vs_chunk_set(output, z, y, x, value);
            }
        }
    }

    return output;
}

// mesh

mesh* vs_mesh_new(f32 *vertices,
                    f32 *normals, // can be NULL if no normals
                    s32 *indices,
                    s32 vertex_count,
                    s32 index_count) {

    mesh* ret = malloc(sizeof(mesh));
    ret->vertices = vertices;
    ret->indices = indices;
    ret->normals = normals;
    ret->vertex_count = vertex_count;
    ret->index_count = index_count;
    return ret;
}


void vs_mesh_free(mesh *mesh) {
    if (mesh) {
        free(mesh->vertices);
        free(mesh->indices);
        free(mesh->normals);
        free(mesh);
    }
}

void vs_mesh_get_bounds(const mesh *m,
                    f32 *origin_z, f32 *origin_y, f32 *origin_x,
                    f32 *length_z, f32 *length_y, f32 *length_x) {
    if (!m || !m->vertices || m->vertex_count <= 0) {
        if (origin_z) *origin_z = 0.0f;
        if (origin_y) *origin_y = 0.0f;
        if (origin_x) *origin_x = 0.0f;
        if (length_z) *length_z = 0.0f;
        if (length_y) *length_y = 0.0f;
        if (length_x) *length_x = 0.0f;
        return;
    }

    f32 min_z = m->vertices[0];
    f32 max_z = m->vertices[0];
    f32 min_y = m->vertices[1];
    f32 max_y = m->vertices[1];
    f32 min_x = m->vertices[2];
    f32 max_x = m->vertices[2];

    for (s32 i = 0; i < m->vertex_count * 3; i += 3) {
        if (m->vertices[i] < min_z) min_z = m->vertices[i];
        if (m->vertices[i] > max_z) max_z = m->vertices[i];

        if (m->vertices[i + 1] < min_y) min_y = m->vertices[i + 1];
        if (m->vertices[i + 1] > max_y) max_y = m->vertices[i + 1];

        if (m->vertices[i + 2] < min_x) min_x = m->vertices[i + 2];
        if (m->vertices[i + 2] > max_x) max_x = m->vertices[i + 2];
    }

    if (origin_z) *origin_z = min_z;
    if (origin_y) *origin_y = min_y;
    if (origin_x) *origin_x = min_x;

    if (length_z) *length_z = max_z - min_z;
    if (length_y) *length_y = max_y - min_y;
    if (length_x) *length_x = max_x - min_x;
}

void vs_mesh_translate(mesh *m, f32 z, f32 y, f32 x) {
    if (!m || !m->vertices || m->vertex_count <= 0) {
        return;
    }

    for (s32 i = 0; i < m->vertex_count * 3; i += 3) {
        m->vertices[i]     += z;  // Z
        m->vertices[i + 1] += y;  // Y
        m->vertices[i + 2] += x;  // X
    }
}

void vs_mesh_scale(mesh *m, f32 scale_z, f32 scale_y, f32 scale_x) {
    if (!m || !m->vertices || m->vertex_count <= 0) {
        return;
    }

    for (s32 i = 0; i < m->vertex_count * 3; i += 3) {
        m->vertices[i]     *= scale_z;  // Z
        m->vertices[i + 1] *= scale_y;  // Y
        m->vertices[i + 2] *= scale_x;  // X
    }

    // If normals are present, we need to renormalize them after non-uniform scaling
    if (m->normals) {
        for (s32 i = 0; i < m->vertex_count * 3; i += 3) {
            // Scale the normal vector
            f32 nz = m->normals[i]     * scale_z;
            f32 ny = m->normals[i + 1] * scale_y;
            f32 nx = m->normals[i + 2] * scale_x;

            // Calculate length for normalization
            f32 length = sqrtf(nz * nz + ny * ny + nx * nx);

            // Avoid division by zero
            if (length > 0.0001f) {
                m->normals[i]     = nz / length;
                m->normals[i + 1] = ny / length;
                m->normals[i + 2] = nx / length;
            }
        }
    }
}

static void vs__interpolate_vertex(f32 isovalue,
                                    f32 v1, f32 v2,
                                    f32 x1, f32 y1, f32 z1,
                                    f32 x2, f32 y2, f32 z2,
                                    f32* out_x, f32* out_y, f32* out_z) {
    if (fabs(isovalue - v1) < 0.00001f) {
        *out_x = x1;
        *out_y = y1;
        *out_z = z1;
        return;
    }
    if (fabs(isovalue - v2) < 0.00001f) {
        *out_x = x2;
        *out_y = y2;
        *out_z = z2;
        return;
    }
    if (fabs(v1 - v2) < 0.00001f) {
        *out_x = x1;
        *out_y = y1;
        *out_z = z1;
        return;
    }

    f32 mu = (isovalue - v1) / (v2 - v1);
    *out_x = x1 + mu * (x2 - x1);
    *out_y = y1 + mu * (y2 - y1);
    *out_z = z1 + mu * (z2 - z1);
}

static f32 vs__get_value(const f32* values, s32 x, s32 y, s32 z,
                            s32 dimx, s32 dimy, s32 dimz) {
    return values[z * (dimx * dimy) + y * dimx + x];
}

static void vs__process_cube(const f32* values,
                        s32 x, s32 y, s32 z,
                        s32 dimx, s32 dimy, s32 dimz,
                        f32 isovalue,
                        f32* vertices,
                        s32* indices,
                        s32* vertex_count,
                        s32* index_count) {


static const s32 edgeTable[256]={
0x0  , 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
0x190, 0x99 , 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
0x230, 0x339, 0x33 , 0x13a, 0x636, 0x73f, 0x435, 0x53c,
0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
0x3a0, 0x2a9, 0x1a3, 0xaa , 0x7a6, 0x6af, 0x5a5, 0x4ac,
0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
0x460, 0x569, 0x663, 0x76a, 0x66 , 0x16f, 0x265, 0x36c,
0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff , 0x3f5, 0x2fc,
0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55 , 0x15c,
0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc ,
0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
0xcc , 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
0x15c, 0x55 , 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
0x2fc, 0x3f5, 0xff , 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
0x36c, 0x265, 0x16f, 0x66 , 0x76a, 0x663, 0x569, 0x460,
0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa , 0x1a3, 0x2a9, 0x3a0,
0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33 , 0x339, 0x230,
0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99 , 0x190,
0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0   };

static const s32 triTable[256][16] =
{ {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1},
{3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1},
{3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1},
{3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1},
{9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1},
{1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
{9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
{2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1},
{8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1},
{9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
{4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1},
{3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1},
{1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1},
{4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1},
{4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1},
{9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1},
{1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
{5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1},
{2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1},
{9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
{0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
{2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1},
{10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1},
{4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1},
{5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1},
{5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1},
{9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1},
{0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
{1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1},
{10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1},
{8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1},
{2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1},
{7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1},
{9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1},
{2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1},
{11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1},
{9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1},
{5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1},
{11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1},
{11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
{1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1},
{9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
{5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1},
{2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
{0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
{5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1},
{6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1},
{0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1},
{3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1},
{6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1},
{5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1},
{1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
{10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1},
{6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
{1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1},
{8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1},
{7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1},
{3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
{5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1},
{0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1},
{9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1},
{8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1},
{5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1},
{0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1},
{6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1},
{10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1},
{10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1},
{8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1},
{1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
{3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1},
{0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1},
{10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1},
{0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1},
{3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1},
{6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1},
{9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1},
{8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1},
{3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1},
{6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1},
{0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1},
{10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1},
{10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1},
{1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1},
{2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1},
{7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1},
{7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1},
{2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1},
{1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1},
{11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1},
{8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1},
{0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1},
{7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
{10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
{2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
{6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1},
{7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1},
{2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1},
{1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1},
{10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
{10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1},
{0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1},
{7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1},
{6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
{8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1},
{9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1},
{6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1},
{1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1},
{4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1},
{10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1},
{8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1},
{0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1},
{1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1},
{8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1},
{10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1},
{4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1},
{10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
{5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
{11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1},
{9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
{6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1},
{7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1},
{3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1},
{7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1},
{9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1},
{3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1},
{6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1},
{9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1},
{1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1},
{4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1},
{7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1},
{6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1},
{3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
{0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1},
{6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1},
{1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1},
{0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1},
{11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1},
{6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1},
{5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1},
{9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1},
{1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1},
{1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1},
{10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1},
{0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1},
{5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1},
{10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1},
{11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1},
{0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1},
{9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1},
{7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1},
{2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1},
{8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1},
{9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1},
{9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1},
{1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1},
{9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1},
{9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1},
{5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1},
{0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1},
{10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1},
{2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1},
{0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1},
{0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1},
{9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1},
{5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1},
{3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1},
{5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1},
{8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1},
{0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1},
{9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1},
{0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1},
{1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1},
{3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1},
{4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1},
{9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1},
{11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1},
{11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1},
{2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1},
{9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1},
{3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1},
{1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1},
{4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1},
{4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1},
{0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1},
{3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1},
{3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1},
{0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1},
{9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1},
{1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1} };

    f32 cube_values[8];
    cube_values[0] = vs__get_value(values, x, y, z, dimx, dimy, dimz);
    cube_values[1] = vs__get_value(values, x + 1, y, z, dimx, dimy, dimz);
    cube_values[2] = vs__get_value(values, x + 1, y + 1, z, dimx, dimy, dimz);
    cube_values[3] = vs__get_value(values, x, y + 1, z, dimx, dimy, dimz);
    cube_values[4] = vs__get_value(values, x, y, z + 1, dimx, dimy, dimz);
    cube_values[5] = vs__get_value(values, x + 1, y, z + 1, dimx, dimy, dimz);
    cube_values[6] = vs__get_value(values, x + 1, y + 1, z + 1, dimx, dimy, dimz);
    cube_values[7] = vs__get_value(values, x, y + 1, z + 1, dimx, dimy, dimz);

    s32 cubeindex = 0;
    for (s32 i = 0; i < 8; i++) {
        if (cube_values[i] < isovalue)
            cubeindex |= (1 << i);
    }

    if (edgeTable[cubeindex] == 0)
        return;

    f32 edge_verts[12][3];  // [x,y,z] for each possible edge vertex

    if (edgeTable[cubeindex] & 1)
        vs__interpolate_vertex(isovalue, cube_values[0], cube_values[1],
                         x, y, z,             // vertex 0
                         x + 1, y, z,         // vertex 1
                         &edge_verts[0][0], &edge_verts[0][1], &edge_verts[0][2]);

    if (edgeTable[cubeindex] & 2)
        vs__interpolate_vertex(isovalue, cube_values[1], cube_values[2],
                         x + 1, y, z,         // vertex 1
                         x + 1, y + 1, z,     // vertex 2
                         &edge_verts[1][0], &edge_verts[1][1], &edge_verts[1][2]);

    if (edgeTable[cubeindex] & 4)
        vs__interpolate_vertex(isovalue, cube_values[2], cube_values[3],
                         x + 1, y + 1, z,     // vertex 2
                         x, y + 1, z,         // vertex 3
                         &edge_verts[2][0], &edge_verts[2][1], &edge_verts[2][2]);

    if (edgeTable[cubeindex] & 8)
        vs__interpolate_vertex(isovalue, cube_values[3], cube_values[0],
                         x, y + 1, z,         // vertex 3
                         x, y, z,             // vertex 0
                         &edge_verts[3][0], &edge_verts[3][1], &edge_verts[3][2]);

    if (edgeTable[cubeindex] & 16)
        vs__interpolate_vertex(isovalue, cube_values[4], cube_values[5],
                         x, y, z + 1,         // vertex 4
                         x + 1, y, z + 1,     // vertex 5
                         &edge_verts[4][0], &edge_verts[4][1], &edge_verts[4][2]);

    if (edgeTable[cubeindex] & 32)
        vs__interpolate_vertex(isovalue, cube_values[5], cube_values[6],
                         x + 1, y, z + 1,     // vertex 5
                         x + 1, y + 1, z + 1, // vertex 6
                         &edge_verts[5][0], &edge_verts[5][1], &edge_verts[5][2]);

    if (edgeTable[cubeindex] & 64)
        vs__interpolate_vertex(isovalue, cube_values[6], cube_values[7],
                         x + 1, y + 1, z + 1, // vertex 6
                         x, y + 1, z + 1,     // vertex 7
                         &edge_verts[6][0], &edge_verts[6][1], &edge_verts[6][2]);

    if (edgeTable[cubeindex] & 128)
        vs__interpolate_vertex(isovalue, cube_values[7], cube_values[4],
                         x, y + 1, z + 1,     // vertex 7
                         x, y, z + 1,         // vertex 4
                         &edge_verts[7][0], &edge_verts[7][1], &edge_verts[7][2]);

    if (edgeTable[cubeindex] & 256)
        vs__interpolate_vertex(isovalue, cube_values[0], cube_values[4],
                         x, y, z,             // vertex 0
                         x, y, z + 1,         // vertex 4
                         &edge_verts[8][0], &edge_verts[8][1], &edge_verts[8][2]);

    if (edgeTable[cubeindex] & 512)
        vs__interpolate_vertex(isovalue, cube_values[1], cube_values[5],
                         x + 1, y, z,         // vertex 1
                         x + 1, y, z + 1,     // vertex 5
                         &edge_verts[9][0], &edge_verts[9][1], &edge_verts[9][2]);

    if (edgeTable[cubeindex] & 1024)
        vs__interpolate_vertex(isovalue, cube_values[2], cube_values[6],
                         x + 1, y + 1, z,     // vertex 2
                         x + 1, y + 1, z + 1, // vertex 6
                         &edge_verts[10][0], &edge_verts[10][1], &edge_verts[10][2]);

    if (edgeTable[cubeindex] & 2048)
        vs__interpolate_vertex(isovalue, cube_values[3], cube_values[7],
                         x, y + 1, z,         // vertex 3
                         x, y + 1, z + 1,     // vertex 7
                         &edge_verts[11][0], &edge_verts[11][1], &edge_verts[11][2]);

    for (s32 i = 0; triTable[cubeindex][i] != -1; i += 3) {
        for (s32 j = 0; j < 3; j++) {
            s32 edge = triTable[cubeindex][i + j];
            vertices[*vertex_count * 3] = edge_verts[edge][0];
            vertices[*vertex_count * 3 + 1] = edge_verts[edge][1];
            vertices[*vertex_count * 3 + 2] = edge_verts[edge][2];

            indices[*index_count] = *vertex_count;

            (*vertex_count)++;
            (*index_count)++;
        }
    }
}

s32 vs_march_cubes(const f32* values,
                s32 dimz, s32 dimy, s32 dimx,
                f32 isovalue,
                f32** out_vertices,      //  [z,y,x,z,y,x,...]
                s32** out_indices,
                s32* out_vertex_count,
                s32* out_index_count) {

    s32 max_triangles = (dimx - 1) * (dimy - 1) * (dimz - 1) * 5;

    f32* vertices = malloc(sizeof(f32) * max_triangles * 3 * 3); // 3 vertices per tri, 3 coords per vertex
    s32* indices = malloc(sizeof(s32) * max_triangles * 3);          // 3 indices per triangle

    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        return 0;
    }

    s32 vertex_count = 0;
    s32 index_count = 0;

    for (s32 z = 0; z < dimz - 1; z++) {
        for (s32 y = 0; y < dimy - 1; y++) {
            for (s32 x = 0; x < dimx - 1; x++) {
                vs__process_cube(values, x, y, z, dimx, dimy, dimz,
                           isovalue, vertices, indices,
                           &vertex_count, &index_count);
            }
        }
    }

    // Shrink arrays to actual size
    vertices = realloc(vertices, sizeof(f32) * vertex_count * 3);
    indices = realloc(indices, sizeof(s32) * index_count);

    *out_vertices = vertices;
    *out_indices = indices;
    *out_vertex_count = vertex_count;
    *out_index_count = index_count;

    return 0;
}

// nrrd
static int vs__nrrd_parse_sizes(char* value, nrrd* nrrd) {
    char* token = strtok(value, " ");
    s32 i = 0;
    while (token != NULL && i < nrrd->dimension) {
        nrrd->sizes[i] = atoi(token);
        if (nrrd->sizes[i] <= 0) {
            printf("Invalid size value: %s", token);
            return 1;
        }
        token = strtok(NULL, " ");
        i++;
    }
    return (i == nrrd->dimension) ? 0 : 1;
}

static int vs__nrrd_parse_space_directions(char* value, nrrd* nrrd) {
    char* token = strtok(value, ") (");
    s32 i = 0;
    while (token != NULL && i < nrrd->dimension) {
        if (strcmp(token, "none") == 0) {
            nrrd->space_directions[i][0] = 0;
            nrrd->space_directions[i][1] = 0;
            nrrd->space_directions[i][2] = 0;
        } else {
            if (sscanf(token, "%f,%f,%f",
                      &nrrd->space_directions[i][0],
                      &nrrd->space_directions[i][1],
                      &nrrd->space_directions[i][2]) != 3) {
                printf("Invalid space direction: %s", token);
                return 1;
            }
        }
        token = strtok(NULL, ") (");
        i++;
    }
    return 0;
}

static int vs__nrrd_parse_space_origin(char* value, nrrd* nrrd) {
    value++; // Skip first '('
    value[strlen(value)-1] = '\0'; // Remove last ')'

    if (sscanf(value, "%f,%f,%f",
               &nrrd->space_origin[0],
               &nrrd->space_origin[1],
               &nrrd->space_origin[2]) != 3) {
        printf("Invalid space origin: %s", value);
        return 1;
    }
    return 0;
}

static size_t vs__nrrd_get_type_size(const char* type) {
    if (strcmp(type, "uint8") == 0 || strcmp(type, "uchar") == 0) return 1;
    if (strcmp(type, "uint16") == 0) return 2;
    if (strcmp(type, "uint32") == 0) return 4;
    if (strcmp(type, "f32") == 0) return 4;
    if (strcmp(type, "double") == 0) return 8;
    return 0;
}

static int vs__nrrd_read_raw_data(FILE* fp, nrrd* nrrd) {
    size_t bytes_read = fread(nrrd->data, 1, nrrd->data_size, fp);
    if (bytes_read != nrrd->data_size) {
        printf("Failed to read data: expected %zu bytes, got %zu",
                nrrd->data_size, bytes_read);
        return 1;
    }
    return 0;
}

static int vs__nrrd_read_gzip_data(FILE* fp, nrrd* nrrd) {
    printf("reading compressed data is not supported yet for nrrd\n");
    assert("false");
    return 1;
    #if 0
    z_stream strm = {0};
    unsigned char in[16384];
    size_t bytes_written = 0;

    if (inflateInit2(&strm,-MAX_WBITS) != Z_OK) {
        printf("Failed to initialize zlib");
        return 1;
    }

    s32 ret;
    do {
        strm.avail_in = fread(in, 1, sizeof(in), fp);
        if (ferror(fp)) {
            inflateEnd(&strm);
            printf("Error reading compressed data");
            return 1;
        }
        if (strm.avail_in == 0) break;
        strm.next_in = in;

        do {
            strm.avail_out = nrrd->data_size - bytes_written;
            strm.next_out = (unsigned char*)nrrd->data + bytes_written;
            ret = inflate(&strm, Z_NO_FLUSH);

            if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                inflateEnd(&strm);
                printf("Decompression error: %s", strm.msg);
                return 1;
            }

            bytes_written = nrrd->data_size - strm.avail_out;

        } while (strm.avail_out == 0);

    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return 0;
    #endif
}

nrrd* vs_nrrd_read(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        printf("could not open %s\n",filename);
        return NULL;
    }

    nrrd* ret = calloc(1, sizeof(nrrd));
    if (!ret) {

        printf("could not allocate ram for nrrd\n");
        fclose(fp);
        return NULL;
    }
    ret->is_valid = true;

    char line[MAX_LINE_LENGTH];
    if (!fgets(line, sizeof(line), fp)) {
        printf("Failed to read magic");
        ret->is_valid = false;
        goto cleanup;
    }
    vs__trim(line);

    if (!vs__str_starts_with(line, "NRRD")) {
        printf("Not a NRRD file: %s", line);
        ret->is_valid = false;
        goto cleanup;
    }

    while (fgets(line, sizeof(line), fp)) {
        vs__trim(line);

        // Empty line marks end of header
        if (strlen(line) == 0) break;

        //if we are left with just a newline after vs__trimming then we have a blank line, we are going to
        // start reading data now so we need to break
        if(line[0] == '\n') break;

        // Skip comments
        if (line[0] == '#') continue;

        char* separator = strchr(line, ':');
        if (!separator) continue;

        *separator = '\0';
        char* key = line;
        char* value = separator + 1;
        while (*value == ' ') value++;

        vs__trim(key);
        vs__trim(value);

        if (strcmp(key, "type") == 0) {
            strncpy(ret->type, value, sizeof(ret->type)-1);
        }
        else if (strcmp(key, "dimension") == 0) {
            ret->dimension = atoi(value);
            if (ret->dimension <= 0 || ret->dimension > 16) {
                printf("Invalid dimension: %d", ret->dimension);
                ret->is_valid = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "space") == 0) {
            strncpy(ret->space, value, sizeof(ret->space)-1);
        }
        else if (strcmp(key, "sizes") == 0) {
            if (!vs__nrrd_parse_sizes(value, ret)) {
                ret->is_valid = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "space directions") == 0) {
            if (!vs__nrrd_parse_space_directions(value, ret)) {
                ret->is_valid = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "endian") == 0) {
            strncpy(ret->endian, value, sizeof(ret->endian)-1);
        }
        else if (strcmp(key, "encoding") == 0) {
            strncpy(ret->encoding, value, sizeof(ret->encoding)-1);
        }
        else if (strcmp(key, "space origin") == 0) {
            if (!vs__nrrd_parse_space_origin(value, ret)) {
                ret->is_valid = false;
                goto cleanup;
            }
        }
    }

    size_t type_size = vs__nrrd_get_type_size(ret->type);
    if (type_size == 0) {
        printf("Unsupported type: %s", ret->type);
        ret->is_valid = false;
        goto cleanup;
    }

    ret->data_size = type_size;
    for (s32 i = 0; i < ret->dimension; i++) {
        ret->data_size *= ret->sizes[i];
    }

    ret->data = malloc(ret->data_size);
    if (!ret->data) {
        printf("Failed to allocate %zu bytes", ret->data_size);
        ret->is_valid = false;
        goto cleanup;
    }

    if (strcmp(ret->encoding, "raw") == 0) {
        if (!vs__nrrd_read_raw_data(fp, ret)) {
            ret->is_valid = false;
            goto cleanup;
        }
    }
    else if (strcmp(ret->encoding, "gzip") == 0) {
        if (!vs__nrrd_read_gzip_data(fp, ret)) {
            ret->is_valid = false;
            goto cleanup;
        }
    }
    else {
        printf("Unsupported encoding: %s", ret->encoding);
        ret->is_valid = false;
        goto cleanup;
    }

cleanup:
    fclose(fp);
    if (!ret->is_valid) {
        if (ret->data) free(ret->data);
        free(ret);
        return NULL;
    }
    return ret;
}

void vs_nrrd_free(nrrd* nrrd) {
    if (nrrd) {
        if (nrrd->data) free(nrrd->data);
        free(nrrd);
    }
}

// obj

s32 vs_read_obj(const char* filename,
            f32** vertices, s32** indices,
            s32* vertex_count, s32* index_count) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        return 1;
    }

    size_t vertex_capacity = 1024;
    size_t index_capacity = 1024;
    *vertices = malloc(vertex_capacity * 3 * sizeof(f32));
    *indices = malloc(index_capacity * sizeof(s32));
    *vertex_count = 0;
    *index_count = 0;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == 'v' && line[1] == ' ') {
            if (*vertex_count >= vertex_capacity) {
                vertex_capacity *= 2;
                f32* new_vertices = realloc(*vertices, vertex_capacity * 3 * sizeof(f32));
                if (!new_vertices) {
                    fclose(fp);
                    return 1;
                }
                *vertices = new_vertices;
            }

            // Read vertex coordinates
            f32 x, y, z;
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
                (*vertices)[(*vertex_count) * 3] = x;
                (*vertices)[(*vertex_count) * 3 + 1] = y;
                (*vertices)[(*vertex_count) * 3 + 2] = z;
                (*vertex_count)++;
            }
        }
        else if (line[0] == 'f' && line[1] == ' ') {
            // Parse face indices
            s32 v1, v2, v3, t1, t2, t3, n1, n2, n3;
            s32 matches = sscanf(line + 2, "%d/%d/%d %d/%d/%d %d/%d/%d",
                               &v1, &t1, &n1, &v2, &t2, &n2, &v3, &t3, &n3);

            if (matches != 9) {
                // Try parsing without texture/normal indices
                matches = sscanf(line + 2, "%d %d %d", &v1, &v2, &v3);
                if (matches != 3) {
                    continue;  // Skip malformed faces
                }
            }

            if (*index_count + 3 > index_capacity) {
                index_capacity *= 2;
                s32* new_indices = realloc(*indices, index_capacity * sizeof(s32));
                if (!new_indices) {
                    fclose(fp);
                    return 1;
                }
                *indices = new_indices;
            }

            // Store face indices (converting from 1-based to 0-based)
            (*indices)[(*index_count)++] = v1 - 1;
            (*indices)[(*index_count)++] = v2 - 1;
            (*indices)[(*index_count)++] = v3 - 1;
        }
    }

    // Shrink arrays to actual size
    *vertices = realloc(*vertices, (*vertex_count) * 3 * sizeof(f32));
    *indices = realloc(*indices, (*index_count) * sizeof(s32));

    fclose(fp);
    return 0;
}

s32 vs_write_obj(const char* filename,
             const f32* vertices, const s32* indices,
             s32 vertex_count, s32 index_count) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        return 1;
    }

    fprintf(fp, "# OBJ file created by minilibs/miniobj\n");

    // Write vertices
    for (s32 i = 0; i < vertex_count; i++) {
        fprintf(fp, "v %.6f %.6f %.6f\n",
                vertices[i * 3],
                vertices[i * 3 + 1],
                vertices[i * 3 + 2]);
    }

    // Write faces (converting from 0-based to 1-based indices)
    assert(index_count % 3 == 0);  // Ensure we have complete triangles
    for (s32 i = 0; i < index_count; i += 3) {
        fprintf(fp, "f %d %d %d\n",
                indices[i] + 1,
                indices[i + 1] + 1,
                indices[i + 2] + 1);
    }

    fclose(fp);
    return 0;
}

// ply

//TODO: most the ply files I come across use x y z order. should we swap the order here so they
// end up in the data as z y x?

s32 vs_ply_write(const char *filename,
                    const f32 *vertices,
                    const f32 *normals, // can be NULL if no normals
                    const s32 *indices,
                    s32 vertex_count,
                    s32 index_count) {

  FILE *fp = fopen(filename, "w");
  if (!fp) {
    return 1;
  }

  fprintf(fp, "ply\n");
  fprintf(fp, "format ascii 1.0\n");
  fprintf(fp, "comment Created by minilibs\n");
  fprintf(fp, "element vertex %d\n", vertex_count);
  fprintf(fp, "property float x\n");
  fprintf(fp, "property float y\n");
  fprintf(fp, "property float z\n");

  if (normals) {
    fprintf(fp, "property float nx\n");
    fprintf(fp, "property float ny\n");
    fprintf(fp, "property float nz\n");
  }

  fprintf(fp, "element face %d\n", index_count / 3);
  fprintf(fp, "property list uchar int vertex_indices\n");
  fprintf(fp, "end_header\n");

  for (s32 i = 0; i < vertex_count; i++) {
    if (normals) {
      fprintf(fp, "%.6f %.6f %.6f %.6f %.6f %.6f\n",
              vertices[i * 3],     // x
              vertices[i * 3 + 1], // y
              vertices[i * 3 + 2], // z
              normals[i * 3],     // nx
              normals[i * 3 + 1], // ny
              normals[i * 3 + 2]  // nz
      );
    } else {
      fprintf(fp, "%.6f %.6f %.6f\n",
              vertices[i * 3],     // x
              vertices[i * 3 + 1], // y
              vertices[i * 3 + 2]  // z
      );
    }
  }

  // Write faces
  for (s32 i = 0; i < index_count; i += 3) {
    fprintf(fp, "3 %d %d %d\n",
            indices[i],
            indices[i + 1],
            indices[i + 2]);
  }

  fclose(fp);
  return 0;
}

s32 vs_ply_read(const char *filename,
                          f32 **out_vertices,
                          f32 **out_normals,
                          s32 **out_indices,
                          s32 *out_vertex_count,
                          s32 *out_normal_count,
                          s32 *out_index_count) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    return 1;
  }

  char buffer[1024];
  if (!fgets(buffer, sizeof(buffer), fp) || strncmp(buffer, "ply", 3) != 0) {
    fclose(fp);
    return 1;
  }

  // Check format
  if (!fgets(buffer, sizeof(buffer), fp)) {
    fclose(fp);
    return 1;
  }
  s32 is_binary = (strncmp(buffer, "format binary_little_endian", 26) == 0);
  s32 is_ascii = (strncmp(buffer, "format ascii", 11) == 0);
  if (!is_binary && !is_ascii) {
    fclose(fp);
    return 1;
  }

  s32 vertex_count = 0;
  s32 face_count = 0;
  s32 has_normals = 0;
  s32 in_header = 1;
  s32 got_vertex = 0;
  s32 got_face = 0;
  s32 is_double = 0;  // Track if the file uses doubles

  // Parse header
  while (in_header && fgets(buffer, sizeof(buffer), fp)) {
    if (strncmp(buffer, "end_header", 10) == 0) {
      in_header = 0;
    } else if (strncmp(buffer, "element vertex", 13) == 0) {
      sscanf(buffer, "element vertex %d", &vertex_count);
      got_vertex = 1;
    } else if (strncmp(buffer, "element face", 12) == 0) {
      sscanf(buffer, "element face %d", &face_count);
      got_face = 1;
    } else if (strncmp(buffer, "property double", 14) == 0) {
      is_double = 1;  // File uses doubles
    } else if (strncmp(buffer, "property double nx", 17) == 0) {
      has_normals = 1;
    }
  }

  if (!got_vertex || vertex_count <= 0) {
    fclose(fp);
    return 1;
  }

  // Allocate memory for float32 output
  f32 *vertices = malloc(vertex_count * 3 * sizeof(f32));
  f32 *normals = NULL;
  s32 *indices = NULL;

  if (has_normals) {
    normals = malloc(vertex_count * 3 * sizeof(f32));
    if (!normals) {
      free(vertices);
      fclose(fp);
      return 1;
    }
  }

  if (got_face && face_count > 0) {
    indices = malloc(face_count * 3 * sizeof(s32));
    if (!indices) {
      free(vertices);
      free(normals);
      fclose(fp);
      return 1;
    }
  }

  if (!vertices) {
    free(normals);
    free(indices);
    fclose(fp);
    return 1;
  }

  // Read vertex data
  if (is_binary) {
    if (is_double) {
      // Reading doubles and converting to floats
      double temp[6];  // Temporary buffer for doubles (3 for position, 3 for normals)
      for (s32 i = 0; i < vertex_count; i++) {
        // Read position as double and convert to f32
        if (fread(temp, sizeof(double), 3, fp) != 3) {
          free(vertices);
          free(normals);
          free(indices);
          fclose(fp);
          return 1;
        }
        vertices[i * 3] = (f32)temp[0];
        vertices[i * 3 + 1] = (f32)temp[1];
        vertices[i * 3 + 2] = (f32)temp[2];

        // Read normals if present
        if (has_normals) {
          if (fread(temp, sizeof(double), 3, fp) != 3) {
            free(vertices);
            free(normals);
            free(indices);
            fclose(fp);
            return 1;
          }
          normals[i * 3] = (f32)temp[0];
          normals[i * 3 + 1] = (f32)temp[1];
          normals[i * 3 + 2] = (f32)temp[2];
        }
      }
    } else {
      // Reading floats directly
      for (s32 i = 0; i < vertex_count; i++) {
        // Read position
        if (fread(&vertices[i * 3], sizeof(f32), 3, fp) != 3) {
          free(vertices);
          free(normals);
          free(indices);
          fclose(fp);
          return 1;
        }

        // Read normals if present
        if (has_normals) {
          if (fread(&normals[i * 3], sizeof(f32), 3, fp) != 3) {
            free(vertices);
            free(normals);
            free(indices);
            fclose(fp);
            return 1;
          }
        }
      }
    }
  } else {
    // ASCII reading - read as double and convert to f32
    double temp[6];  // Temporary buffer for doubles
    for (s32 i = 0; i < vertex_count; i++) {
      if (has_normals) {
        if (fscanf(fp, "%lf %lf %lf %lf %lf %lf",
                   &temp[0], &temp[1], &temp[2],
                   &temp[3], &temp[4], &temp[5]) != 6) {
          free(vertices);
          free(normals);
          free(indices);
          fclose(fp);
          return 1;
        }
        vertices[i * 3] = (f32)temp[0];
        vertices[i * 3 + 1] = (f32)temp[1];
        vertices[i * 3 + 2] = (f32)temp[2];
        normals[i * 3] = (f32)temp[3];
        normals[i * 3 + 1] = (f32)temp[4];
        normals[i * 3 + 2] = (f32)temp[5];
      } else {
        if (fscanf(fp, "%lf %lf %lf",
                   &temp[0], &temp[1], &temp[2]) != 3) {
          free(vertices);
          free(normals);
          free(indices);
          fclose(fp);
          return 1;
        }
        vertices[i * 3] = (f32)temp[0];
        vertices[i * 3 + 1] = (f32)temp[1];
        vertices[i * 3 + 2] = (f32)temp[2];
      }
    }
  }

  // Read face data if present
  s32 index_count = 0;
  if (got_face && indices) {
    if (is_binary) {
      for (s32 i = 0; i < face_count; i++) {
        unsigned char vertex_per_face;
        if (fread(&vertex_per_face, sizeof(unsigned char), 1, fp) != 1 || vertex_per_face != 3) {
          free(vertices);
          free(normals);
          free(indices);
          fclose(fp);
          return 1;
        }

        if (fread(&indices[index_count], sizeof(s32), 3, fp) != 3) {
          free(vertices);
          free(normals);
          free(indices);
          fclose(fp);
          return 1;
        }
        index_count += 3;
      }
    } else {
      for (s32 i = 0; i < face_count; i++) {
        s32 vertex_per_face;
        if (fscanf(fp, "%d", &vertex_per_face) != 1 || vertex_per_face != 3) {
          free(vertices);
          free(normals);
          free(indices);
          fclose(fp);
          return 1;
        }

        if (fscanf(fp, "%d %d %d",
                   &indices[index_count],
                   &indices[index_count + 1],
                   &indices[index_count + 2]) != 3) {
          free(vertices);
          free(normals);
          free(indices);
          fclose(fp);
          return 1;
        }
        index_count += 3;
      }
    }
  }

  fclose(fp);

  *out_vertices = vertices;
  *out_normals = normals;
  *out_indices = indices;
  *out_vertex_count = vertex_count;
  *out_normal_count = has_normals ? vertex_count : 0;
  *out_index_count = index_count;

  return 0;
}

// ppm

ppm* vs_ppm_new(u32 width, u32 height) {
    ppm* img = malloc(sizeof(ppm));
    if (!img) {
        return NULL;
    }

    img->width = width;
    img->height = height;
    img->max_val = 255;
    img->data = calloc(width * height * 3, sizeof(u8));

    if (!img->data) {
        free(img);
        return NULL;
    }

    return img;
}

void vs_ppm_free(ppm* img) {
    if (img) {
        free(img->data);
        free(img);
    }
}

static void vs__skip_whitespace_and_comments(FILE* fp) {
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '#') {
            // Skip until end of line
            while ((c = fgetc(fp)) != EOF && c != '\n');
        } else if (!isspace(c)) {
            ungetc(c, fp);
            break;
        }
    }
}

static bool vs__ppm_read_header(FILE* fp, ppm_type* type, u32* width, u32* height, u8* max_val) {
    char magic[3];

    if (fgets(magic, sizeof(magic), fp) == NULL) {
        return false;
    }

    if (magic[0] != 'P' || (magic[1] != '3' && magic[1] != '6')) {
        return false;
    }

    *type = magic[1] == '3' ? P3 : P6;

    vs__skip_whitespace_and_comments(fp);

    if (fscanf(fp, "%u %u", width, height) != 2) {
        return false;
    }

    vs__skip_whitespace_and_comments(fp);

    unsigned int max_val_temp;
    if (fscanf(fp, "%u", &max_val_temp) != 1 || max_val_temp > 255) {
        return false;
    }
    *max_val = (u8)max_val_temp;

    fgetc(fp);

    return true;
}

ppm* vs_ppm_read(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return NULL;
    }

    ppm_type type;
    u32 width, height;
    u8 max_val;

    if (!vs__ppm_read_header(fp, &type, &width, &height, &max_val)) {
        fclose(fp);
        return NULL;
    }

    ppm* img = vs_ppm_new(width, height);
    if (!img) {
        fclose(fp);
        return NULL;
    }

    img->max_val = max_val;
    size_t pixel_count = width * height * 3;

    if (type == P3) {
        // ASCII format
        for (size_t i = 0; i < pixel_count; i++) {
            unsigned int val;
            if (fscanf(fp, "%u", &val) != 1 || val > max_val) {
                vs_ppm_free(img);
                fclose(fp);
                return NULL;
            }
            img->data[i] = (u8)val;
        }
    } else {
        // Binary format
        if (fread(img->data, 1, pixel_count, fp) != pixel_count) {
            vs_ppm_free(img);
            fclose(fp);
            fclose(fp);
            return NULL;
        }
    }

    fclose(fp);
    return img;
}

int vs_ppm_write(const char* filename, const ppm* img, ppm_type type) {
    if (!img || !img->data) {
        return 1;
    }

    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        return 1;
    }

    // Write header
    fprintf(fp, "P%c\n", type == P3 ? '3' : '6');
    fprintf(fp, "%u %u\n", img->width, img->height);
    fprintf(fp, "%u\n", img->max_val);

    size_t pixel_count = img->width * img->height * 3;

    if (type == P3) {
        // ASCII format
        for (size_t i = 0; i < pixel_count; i++) {
            fprintf(fp, "%u", img->data[i]);
            fprintf(fp, (i + 1) % 3 == 0 ? "\n" : " ");
        }
    } else {
        // Binary format
        if (fwrite(img->data, 1, pixel_count, fp) != pixel_count) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

void vs_ppm_set_pixel(ppm* img, u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!img || x >= img->width || y >= img->height) {
        return;
    }

    size_t idx = (y * img->width + x) * 3;
    img->data[idx] = r;
    img->data[idx + 1] = g;
    img->data[idx + 2] = b;
}

void vs_ppm_get_pixel(const ppm* img, u32 x, u32 y, u8* r, u8* g, u8* b) {
    if (!img || x >= img->width || y >= img->height) {
        *r = *g = *b = 0;
        return;
    }

    size_t idx = (y * img->width + x) * 3;
    *r = img->data[idx];
    *g = img->data[idx + 1];
    *b = img->data[idx + 2];
}

// tiff
static uint32_t vs__tiff_read_bytes(FILE* fp, int count, int littleEndian) {
    uint32_t value = 0;
    uint8_t byte;

    if (littleEndian) {
        for (int i = 0; i < count; i++) {
            if (fread(&byte, 1, 1, fp) != 1) return 0;
            value |= ((uint32_t)byte << (i * 8));
        }
    } else {
        for (int i = 0; i < count; i++) {
            if (fread(&byte, 1, 1, fp) != 1) return 0;
            value = (value << 8) | byte;
        }
    }

    return value;
}

static void vs__tiff_read_string(FILE* fp, char* str, uint32_t offset, uint32_t count, long currentPos) {
    long savedPos = ftell(fp);
    fseek(fp, offset, SEEK_SET);
    fread(str, 1, count - 1, fp);
    str[count - 1] = '\0';
    fseek(fp, savedPos, SEEK_SET);
}

static float vs__tiff_read_rational(FILE* fp, uint32_t offset, int littleEndian, long currentPos) {
    long savedPos = ftell(fp);
    fseek(fp, offset, SEEK_SET);
    uint32_t numerator = vs__tiff_read_bytes(fp, 4, littleEndian);
    uint32_t denominator = vs__tiff_read_bytes(fp, 4, littleEndian);
    fseek(fp, savedPos, SEEK_SET);
    return denominator ? (float)numerator / denominator : 0.0f;
}

static void vs__tiff_read_ifd_entry(FILE* fp, DirectoryInfo* dir, int littleEndian, long ifdStart) {
    uint16_t tag = vs__tiff_read_bytes(fp, 2, littleEndian);
    uint16_t type = vs__tiff_read_bytes(fp, 2, littleEndian);
    uint32_t count = vs__tiff_read_bytes(fp, 4, littleEndian);
    uint32_t valueOffset = vs__tiff_read_bytes(fp, 4, littleEndian);

    long currentPos = ftell(fp);

    switch (tag) {
        case TIFFTAG_SUBFILETYPE:
            dir->subfileType = valueOffset;
            break;
        case TIFFTAG_IMAGEWIDTH:
            dir->width = (type == TIFF_SHORT) ? (uint16_t)valueOffset : valueOffset;
            break;
        case TIFFTAG_IMAGELENGTH:
            dir->height = (type == TIFF_SHORT) ? (uint16_t)valueOffset : valueOffset;
            break;
        case TIFFTAG_BITSPERSAMPLE:
            dir->bitsPerSample = (uint16_t)valueOffset;
            break;
        case TIFFTAG_COMPRESSION:
            dir->compression = (uint16_t)valueOffset;
            break;
        case TIFFTAG_PHOTOMETRIC:
            dir->photometric = (uint16_t)valueOffset;
            break;
        case TIFFTAG_IMAGEDESCRIPTION:
            vs__tiff_read_string(fp, dir->imageDescription, valueOffset, count, currentPos);
            break;
        case TIFFTAG_SOFTWARE:
            vs__tiff_read_string(fp, dir->software, valueOffset, count, currentPos);
            break;
        case TIFFTAG_DATETIME:
            vs__tiff_read_string(fp, dir->dateTime, valueOffset, count, currentPos);
            break;
        case TIFFTAG_SAMPLESPERPIXEL:
            dir->samplesPerPixel = (uint16_t)valueOffset;
            break;
        case TIFFTAG_ROWSPERSTRIP:
            dir->rowsPerStrip = (type == TIFF_SHORT) ? (uint16_t)valueOffset : valueOffset;
            break;
        case TIFFTAG_PLANARCONFIG:
            dir->planarConfig = (uint16_t)valueOffset;
            break;
        case TIFFTAG_XRESOLUTION:
            dir->xResolution = vs__tiff_read_rational(fp, valueOffset, littleEndian, currentPos);
            break;
        case TIFFTAG_YRESOLUTION:
            dir->yResolution = vs__tiff_read_rational(fp, valueOffset, littleEndian, currentPos);
            break;
        case TIFFTAG_RESOLUTIONUNIT:
            dir->resolutionUnit = (uint16_t)valueOffset;
            break;
        case TIFFTAG_SAMPLEFORMAT:
            dir->sampleFormat = (uint16_t)valueOffset;
            break;
        case TIFFTAG_STRIPOFFSETS:
            dir->stripInfo.offset = (type == TIFF_SHORT) ? (uint16_t)valueOffset : valueOffset;
            break;
        case TIFFTAG_STRIPBYTECOUNTS:
            dir->stripInfo.byteCount = (type == TIFF_SHORT) ? (uint16_t)valueOffset : valueOffset;
            break;
        default:
            assert(false);
            break;
    }
}

static bool vs__tiff_validate_directory(DirectoryInfo* dir, TiffImage* img) {
    if (dir->width == 0 || dir->height == 0) {
        snprintf(img->errorMsg, sizeof(img->errorMsg), "Invalid dimensions");
        return false;
    }

    if (dir->bitsPerSample != 8 && dir->bitsPerSample != 16) {
        snprintf(img->errorMsg, sizeof(img->errorMsg),
                "Unsupported bits per sample: %d", dir->bitsPerSample);
        return false;
    }

    if (dir->compression != 1) {
        snprintf(img->errorMsg, sizeof(img->errorMsg),
                "Unsupported compression: %d", dir->compression);
        return false;
    }

    if (dir->samplesPerPixel != 1) {
        snprintf(img->errorMsg, sizeof(img->errorMsg),
                "Only single channel images supported");
        return false;
    }

    if (dir->planarConfig != 1) {
        snprintf(img->errorMsg, sizeof(img->errorMsg),
                "Only contiguous data supported");
        return false;
    }

    size_t expectedSize = dir->width * dir->height * (dir->bitsPerSample / 8);
    if (dir->stripInfo.byteCount != expectedSize) {
        snprintf(img->errorMsg, sizeof(img->errorMsg), "Data size mismatch");
        return false;
    }

    return true;
}

TiffImage* vs_tiff_read(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return NULL;

    TiffImage* img = calloc(1, sizeof(TiffImage));
    if (!img) {
        fclose(fp);
        return NULL;
    }

    img->isValid = true;

    uint16_t byteOrder = vs__tiff_read_bytes(fp, 2, 1);
    int littleEndian = (byteOrder == 0x4949);

    if (byteOrder != 0x4949 && byteOrder != 0x4D4D) {
        img->isValid = false;
        snprintf(img->errorMsg, sizeof(img->errorMsg), "Invalid byte order marker");
        fclose(fp);
        return img;
    }

    if (vs__tiff_read_bytes(fp, 2, littleEndian) != 42) {
        img->isValid = false;
        snprintf(img->errorMsg, sizeof(img->errorMsg), "Invalid TIFF version");
        fclose(fp);
        return img;
    }

    // First pass: count directories
    uint32_t ifdOffset = vs__tiff_read_bytes(fp, 4, littleEndian);
    img->depth = 0;
    uint32_t nextIFD = ifdOffset;

    while (nextIFD != 0) {
        img->depth++;
        fseek(fp, nextIFD, SEEK_SET);
        uint16_t numEntries = vs__tiff_read_bytes(fp, 2, littleEndian);
        fseek(fp, 12 * numEntries, SEEK_CUR);  // Skip entries
        nextIFD = vs__tiff_read_bytes(fp, 4, littleEndian);
    }

    // Allocate directory info array
    img->directories = calloc(img->depth, sizeof(DirectoryInfo));
    if (!img->directories) {
        img->isValid = false;
        snprintf(img->errorMsg, sizeof(img->errorMsg), "Memory allocation failed");
        fclose(fp);
        return img;
    }

    // Second pass: read directory information
    nextIFD = ifdOffset;
    int dirIndex = 0;

    while (nextIFD != 0 && img->isValid) {
        DirectoryInfo* currentDir = &img->directories[dirIndex];

        // Set defaults
        currentDir->samplesPerPixel = 1;
        currentDir->planarConfig = 1;
        currentDir->sampleFormat = 1;
        currentDir->compression = 1;

        fseek(fp, nextIFD, SEEK_SET);
        long ifdStart = ftell(fp);

        uint16_t numEntries = vs__tiff_read_bytes(fp, 2, littleEndian);

        for (int i = 0; i < numEntries && img->isValid; i++) {
            vs__tiff_read_ifd_entry(fp, currentDir, littleEndian, ifdStart);
        }

        if (!vs__tiff_validate_directory(currentDir, img)) {
            img->isValid = false;
            break;
        }

        nextIFD = vs__tiff_read_bytes(fp, 4, littleEndian);
        dirIndex++;
    }

    if (img->isValid) {
        DirectoryInfo* firstDir = &img->directories[0];
        size_t sliceSize = firstDir->width * firstDir->height * (firstDir->bitsPerSample / 8);
        img->dataSize = sliceSize * img->depth;
        img->data = malloc(img->dataSize);

        if (!img->data) {
            img->isValid = false;
            snprintf(img->errorMsg, sizeof(img->errorMsg), "Memory allocation failed");
        } else {
            for (int i = 0; i < img->depth && img->isValid; i++) {
                DirectoryInfo* dir = &img->directories[i];
                fseek(fp, dir->stripInfo.offset, SEEK_SET);
                size_t bytesRead = fread((uint8_t*)img->data + (i * sliceSize), 1,
                                       dir->stripInfo.byteCount, fp);
                if (bytesRead != dir->stripInfo.byteCount) {
                    img->isValid = false;
                    snprintf(img->errorMsg, sizeof(img->errorMsg),
                            "Failed to read image data for directory %d", i);
                }
            }
        }
    }

    fclose(fp);
    return img;
}

void vs_tiff_free(TiffImage* img) {
    if (img) {
        free(img->directories);
        free(img->data);
        free(img);
    }
}

const char* vs_tiff_compression_name(uint16_t compression) {
    switch (compression) {
        case 1: return "None";
        case 2: return "CCITT modified Huffman RLE";
        case 3: return "CCITT Group 3 fax encoding";
        case 4: return "CCITT Group 4 fax encoding";
        case 5: return "LZW";
        case 6: return "JPEG (old-style)";
        case 7: return "JPEG";
        case 8: return "Adobe Deflate";
        case 32773: return "PackBits compression";
        default: return "Unknown";
    }
}

const char* vs_tiff_photometric_name(uint16_t photometric) {
    switch (photometric) {
        case 0: return "min-is-white";
        case 1: return "min-is-black";
        case 2: return "RGB";
        case 3: return "palette color";
        case 4: return "transparency mask";
        case 5: return "CMYK";
        case 6: return "YCbCr";
        case 8: return "CIELab";
        default: return "Unknown";
    }
}

const char* vs_tiff_planar_config_name(uint16_t config) {
    switch (config) {
        case 1: return "single image plane";
        case 2: return "separate image planes";
        default: return "Unknown";
    }
}

const char* vs_tiff_sample_format_name(uint16_t format) {
    switch (format) {
        case 1: return "unsigned integer";
        case 2: return "signed integer";
        case 3: return "IEEE floating point";
        case 4: return "undefined";
        default: return "Unknown";
    }
}

const char* vs_tiff_resolution_unit_name(uint16_t unit) {
    switch (unit) {
        case 1: return "unitless";
        case 2: return "inches";
        case 3: return "centimeters";
        default: return "Unknown";
    }
}

void vs_tiff_print_tags(const TiffImage* img, int directory) {
    if (!img || !img->directories || directory >= img->depth) return;

    const DirectoryInfo* dir = &img->directories[directory];

    printf("\n=== TIFF directory %d ===\n", directory);
    printf("TIFF Directory %d\n", directory);

    if (dir->subfileType != 0) {
        printf("  Subfile Type: (%d = 0x%x)\n", dir->subfileType, dir->subfileType);
    }

    printf("  Image Width: %u Image Length: %u\n", dir->width, dir->height);

    if (dir->xResolution != 0 || dir->yResolution != 0) {
        printf("  Resolution: %g, %g (%s)\n",
               dir->xResolution, dir->yResolution,
               vs_tiff_resolution_unit_name(dir->resolutionUnit));
    }

    printf("  Bits/Sample: %u\n", dir->bitsPerSample);
    printf("  Sample Format: %s\n", vs_tiff_sample_format_name(dir->sampleFormat));
    printf("  Compression Scheme: %s\n", vs_tiff_compression_name(dir->compression));
    printf("  Photometric Interpretation: %s\n", vs_tiff_photometric_name(dir->photometric));
    printf("  Samples/Pixel: %u\n", dir->samplesPerPixel);

    if (dir->rowsPerStrip) {
        printf("  Rows/Strip: %u\n", dir->rowsPerStrip);
    }

    printf("  Planar Configuration: %s\n", vs_tiff_planar_config_name(dir->planarConfig));

    if (dir->imageDescription[0]) {
        printf("  ImageDescription: %s\n", dir->imageDescription);
    }
    if (dir->software[0]) {
        printf("  Software: %s\n", dir->software);
    }
    if (dir->dateTime[0]) {
        printf("  DateTime: %s\n", dir->dateTime);
    }
}

void vs_tiff_print_all_tags(const TiffImage* img) {
    if (!img) {
        printf("Error: NULL TIFF image\n");
        return;
    }

    if (!img->isValid) {
        printf("Error reading TIFF: %s\n", img->errorMsg);
        return;
    }

    for (int i = 0; i < img->depth; i++) {
        vs_tiff_print_tags(img, i);
    }
}


size_t vs_tiff_directory_size(const TiffImage* img, int directory) {
    if (!img || !img->isValid || !img->directories || directory >= img->depth) {
        return 0;
    }

    const DirectoryInfo* dir = &img->directories[directory];
    return dir->width * dir->height * (dir->bitsPerSample / 8);
}

void* vs_tiff_read_directory_data(const TiffImage* img, int directory) {

    size_t bufferSize = vs_tiff_directory_size(img, directory);
    void* buffer = malloc(bufferSize);

    if (!img || !img->isValid || !img->directories || !buffer || directory >= img->depth) {
        return NULL;
    }

    const DirectoryInfo* dir = &img->directories[directory];
    size_t sliceSize = dir->width * dir->height * (dir->bitsPerSample / 8);

    if (bufferSize < sliceSize) {
        return NULL;
    }

    size_t offset = sliceSize * directory;
    memcpy(buffer, (uint8_t*)img->data + offset, sliceSize);

    return buffer;
}

uint16_t vs_tiff_pixel16(const uint16_t* buffer, int y, int x, int width) {
    return buffer[ y * width + x];
}

uint8_t vs_tiff_pixel8(const uint8_t* buffer, int y, int x, int width) {
    return buffer[y * width + x];
}


static void vs__tiff_write_bytes(FILE* fp, uint32_t value, int count, int littleEndian) {
    if (littleEndian) {
        for (int i = 0; i < count; i++) {
            uint8_t byte = (value >> (i * 8)) & 0xFF;
            fwrite(&byte, 1, 1, fp);
        }
    } else {
        for (int i = count - 1; i >= 0; i--) {
            uint8_t byte = (value >> (i * 8)) & 0xFF;
            fwrite(&byte, 1, 1, fp);
        }
    }
}

static void vs__tiff_write_string(FILE* fp, const char* str, uint32_t offset) {
    fseek(fp, offset, SEEK_SET);
    size_t len = strlen(str);
    fwrite(str, 1, len + 1, fp);  // Include null terminator
}

static void vs__tiff_write_rational(FILE* fp, float value, uint32_t offset, int littleEndian) {
    fseek(fp, offset, SEEK_SET);
    uint32_t numerator = (uint32_t)(value * 1000);
    uint32_t denominator = 1000;
    vs__tiff_write_bytes(fp, numerator, 4, littleEndian);
    vs__tiff_write_bytes(fp, denominator, 4, littleEndian);
}

static void vs__tiff_current_date_time(char* dateTime) {
    time_t now;
    struct tm* timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    strftime(dateTime, 20, "%Y:%m:%d %H:%M:%S", timeinfo);
}

static uint32_t vs__tiff_write_ifd_entry(FILE* fp, uint16_t tag, uint16_t type, uint32_t count,
                             uint32_t value, int littleEndian) {
    vs__tiff_write_bytes(fp, tag, 2, littleEndian);
    vs__tiff_write_bytes(fp, type, 2, littleEndian);
    vs__tiff_write_bytes(fp, count, 4, littleEndian);
    vs__tiff_write_bytes(fp, value, 4, littleEndian);
    return 12;  // Size of IFD entry
}

int vs_tiff_write(const char* filename, const TiffImage* img, bool littleEndian) {
    if (!img || !img->directories || !img->data || !img->isValid) return 1;

    FILE* fp = fopen(filename, "wb");
    if (!fp) return 1;

    // Write header
    vs__tiff_write_bytes(fp, littleEndian ? 0x4949 : 0x4D4D, 2, 1);  // Byte order marker
    vs__tiff_write_bytes(fp, 42, 2, littleEndian);                    // TIFF version

    uint32_t ifdOffset = 8;  // Start first IFD after header
    vs__tiff_write_bytes(fp, ifdOffset, 4, littleEndian);

    // Calculate space needed for string and rational values
    uint32_t extraDataOffset = ifdOffset;
    for (int d = 0; d < img->depth; d++) {
        extraDataOffset += 2 + (12 * 17) + 4;  // Directory entry count + entries + next IFD pointer
    }

    // Write each directory
    for (int d = 0; d < img->depth; d++) {
        const DirectoryInfo* dir = &img->directories[d];

        // Position at IFD start
        fseek(fp, ifdOffset, SEEK_SET);

        // Write number of directory entries
        vs__tiff_write_bytes(fp, 17, 2, littleEndian);  // Number of IFD entries

        // Write directory entries
        vs__tiff_write_ifd_entry(fp, TIFFTAG_SUBFILETYPE, TIFF_LONG, 1, dir->subfileType, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_IMAGEWIDTH, TIFF_LONG, 1, dir->width, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_IMAGELENGTH, TIFF_LONG, 1, dir->height, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_BITSPERSAMPLE, TIFF_SHORT, 1, dir->bitsPerSample, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_COMPRESSION, TIFF_SHORT, 1, dir->compression, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_PHOTOMETRIC, TIFF_SHORT, 1, dir->photometric, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_SAMPLESPERPIXEL, TIFF_SHORT, 1, dir->samplesPerPixel, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_ROWSPERSTRIP, TIFF_LONG, 1, dir->rowsPerStrip, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_PLANARCONFIG, TIFF_SHORT, 1, dir->planarConfig, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_SAMPLEFORMAT, TIFF_SHORT, 1, dir->sampleFormat, littleEndian);

        // Write resolution entries
        vs__tiff_write_ifd_entry(fp, TIFFTAG_XRESOLUTION, TIFF_RATIONAL, 1, extraDataOffset, littleEndian);
        vs__tiff_write_rational(fp, dir->xResolution, extraDataOffset, littleEndian);
        extraDataOffset += 8;

        vs__tiff_write_ifd_entry(fp, TIFFTAG_YRESOLUTION, TIFF_RATIONAL, 1, extraDataOffset, littleEndian);
        vs__tiff_write_rational(fp, dir->yResolution, extraDataOffset, littleEndian);
        extraDataOffset += 8;

        vs__tiff_write_ifd_entry(fp, TIFFTAG_RESOLUTIONUNIT, TIFF_SHORT, 1, dir->resolutionUnit, littleEndian);

        // Write metadata strings if present
        if (dir->imageDescription[0]) {
            size_t len = strlen(dir->imageDescription) + 1;
            vs__tiff_write_ifd_entry(fp, TIFFTAG_IMAGEDESCRIPTION, TIFF_ASCII, len, extraDataOffset, littleEndian);
            vs__tiff_write_string(fp, dir->imageDescription, extraDataOffset);
            extraDataOffset += len;
        }

        if (dir->software[0]) {
            size_t len = strlen(dir->software) + 1;
            vs__tiff_write_ifd_entry(fp, TIFFTAG_SOFTWARE, TIFF_ASCII, len, extraDataOffset, littleEndian);
            vs__tiff_write_string(fp, dir->software, extraDataOffset);
            extraDataOffset += len;
        }

        if (dir->dateTime[0]) {
            vs__tiff_write_ifd_entry(fp, TIFFTAG_DATETIME, TIFF_ASCII, 20, extraDataOffset, littleEndian);
            vs__tiff_write_string(fp, dir->dateTime, extraDataOffset);
            extraDataOffset += 20;
        }

        // Calculate strip size and write strip information
        size_t stripSize = dir->width * dir->height * (dir->bitsPerSample / 8);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_STRIPOFFSETS, TIFF_LONG, 1, extraDataOffset, littleEndian);
        vs__tiff_write_ifd_entry(fp, TIFFTAG_STRIPBYTECOUNTS, TIFF_LONG, 1, stripSize, littleEndian);

        // Write image data
        fseek(fp, extraDataOffset, SEEK_SET);
        size_t offset = stripSize * d;
        fwrite((uint8_t*)img->data + offset, 1, stripSize, fp);
        extraDataOffset += stripSize;

        // Write next IFD offset or 0 if last directory
        uint32_t nextIFD = (d < img->depth - 1) ? extraDataOffset : 0;
        vs__tiff_write_bytes(fp, nextIFD, 4, littleEndian);

        ifdOffset = nextIFD;
    }

    fclose(fp);
    return 0;
}

TiffImage* vs_tiff_create(uint32_t width, uint32_t height, uint16_t depth,
                           uint16_t bitsPerSample) {
    TiffImage* img = calloc(1, sizeof(TiffImage));
    if (!img) return NULL;

    img->depth = depth;
    img->directories = calloc(depth, sizeof(DirectoryInfo));
    if (!img->directories) {
        free(img);
        return NULL;
    }

    img->dataSize = width * height * (bitsPerSample / 8) * depth;
    img->data = calloc(1, img->dataSize);
    if (!img->data) {
        free(img->directories);
        free(img);
        return NULL;
    }

    // Initialize each directory
    for (int i = 0; i < depth; i++) {
        DirectoryInfo* dir = &img->directories[i];
        dir->width = width;
        dir->height = height;
        dir->bitsPerSample = bitsPerSample;
        dir->compression = 1;  // No compression
        dir->photometric = 1;  // min-is-black
        dir->samplesPerPixel = 1;
        dir->rowsPerStrip = height;
        dir->planarConfig = 1;
        dir->sampleFormat = 1;  // unsigned integer
        dir->xResolution = 72.0f;
        dir->yResolution = 72.0f;
        dir->resolutionUnit = 2;  // ?
        dir->subfileType = 0;

        vs__tiff_current_date_time(dir->dateTime);
    }

    img->isValid = true;
    return img;
}

        
// vcps

static int vs__vcps_read_binary_data(FILE* fp, void* out_data, const char* src_type, const char* dst_type, size_t count) {
    // Fast path: types match, direct read
    if (strcmp(src_type, dst_type) == 0) {
        size_t element_size = strcmp(src_type, "float") == 0 ? sizeof(f32) : sizeof(f64);
        return fread(out_data, element_size, count, fp) == count ? 0 : 1;
    }

    // Conversion path
    if (strcmp(src_type, "double") == 0 && strcmp(dst_type, "float") == 0) {
        f64* temp = malloc(count * sizeof(f64));
        if (!temp) return 1;

        int status = fread(temp, sizeof(f64), count, fp) == count ? 0 : 1;
        if (status == 0) {
            f32* out = out_data;
            for (size_t i = 0; i < count; i++) {
                out[i] = (f32)temp[i];
            }
        }
        free(temp);
        return status;
    }
    else if (strcmp(src_type, "float") == 0 && strcmp(dst_type, "double") == 0) {
        f32* temp = malloc(count * sizeof(f32));
        if (!temp) return 1;

        int status = fread(temp, sizeof(f32), count, fp) == count ? 0 : 1;
        if (status == 0) {
            f64* out = out_data;
            for (size_t i = 0; i < count; i++) {
                out[i] = (f64)temp[i];
            }
        }
        free(temp);
        return status;
    }

    return 1;
}

static int vs__vcps_write_binary_data(FILE* fp, const void* data, const char* src_type, const char* dst_type, size_t count) {
    // Fast path: types match, direct write
    if (strcmp(src_type, dst_type) == 0) {
        size_t element_size = strcmp(src_type, "float") == 0 ? sizeof(f32) : sizeof(f64);
        return fwrite(data, element_size, count, fp) == count ? 0 : 1;
    }

    // Conversion path
    if (strcmp(src_type, "float") == 0 && strcmp(dst_type, "double") == 0) {
        f64* temp = malloc(count * sizeof(f64));
        if (!temp) return 1;

        const f32* in = data;
        for (size_t i = 0; i < count; i++) {
            temp[i] = (f64)in[i];
        }

        int status = fwrite(temp, sizeof(f64), count, fp) == count ? 0 : 1;
        free(temp);
        return status;
    }
    else if (strcmp(src_type, "double") == 0 && strcmp(dst_type, "float") == 0) {
        f32* temp = malloc(count * sizeof(f32));
        if (!temp) return 1;

        const f64* in = data;
        for (size_t i = 0; i < count; i++) {
            temp[i] = (f32)in[i];
        }

        int status = fwrite(temp, sizeof(f32), count, fp) == count ? 0 : 1;
        free(temp);
        return status;
    }

    return 1;
}


int vs_vcps_read(const char* filename,
              size_t* width, size_t* height, size_t* dim,
              void* data, const char* dst_type) {
    if (!dst_type || (strcmp(dst_type, "float") != 0 && strcmp(dst_type, "double") != 0)) {
        fprintf(stderr, "Error: Invalid destination type\n");
        return 1;
    }

    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return 1;
    }

    // Read header
    char line[256];
    int header_complete = 0;
    int ordered = 0;
    char src_type[32] = {0};
    int version = 0;
    *width = 0;
    *height = 0;
    *dim = 0;

    while (fgets(line, sizeof(line), fp)) {
        vs__trim(line);

        if (strcmp(line, "<>") == 0) {
            header_complete = 1;
            break;
        }

        char key[32], value[32];
        if (sscanf(line, "%31[^:]: %31s", key, value) == 2) {
            if (strcmp(key, "width") == 0) {
                *width = atoi(value);
            } else if (strcmp(key, "height") == 0) {
                *height = atoi(value);
            } else if (strcmp(key, "dim") == 0) {
                *dim = atoi(value);
            } else if (strcmp(key, "type") == 0) {
                strncpy(src_type, value, sizeof(src_type) - 1);
            } else if (strcmp(key, "version") == 0) {
                version = atoi(value);
            } else if (strcmp(key, "ordered") == 0) {
                ordered = (strcmp(value, "true") == 0);
            }
        }
    }

    if (!header_complete || *width == 0 || *height == 0 || *dim == 0 ||
        (strcmp(src_type, "float") != 0 && strcmp(src_type, "double") != 0) ||
        !ordered) {
        fprintf(stderr, "Error: Invalid header (w=%zu h=%zu d=%zu t=%s o=%d)\n",
                *width, *height, *dim, src_type, ordered);
        fclose(fp);
        return 1;
    }

    size_t total_points = (*width) * (*height) * (*dim);
    int status = vs__vcps_read_binary_data(fp, data, src_type, dst_type, total_points);

    fclose(fp);
    return status;
}

int vs_vcps_write(const char* filename,
               size_t width, size_t height, size_t dim,
               const void* data, const char* src_type, const char* dst_type) {
    if (!src_type || !dst_type ||
        (strcmp(src_type, "float") != 0 && strcmp(src_type, "double") != 0) ||
        (strcmp(dst_type, "float") != 0 && strcmp(dst_type, "double") != 0)) {
        fprintf(stderr, "Error: Invalid type specification\n");
        return 1;
    }

    FILE* fp = fopen(filename, "w");
    if (!fp) return 1;

    // Write header
    fprintf(fp, "width: %zu\n", width);
    fprintf(fp, "height: %zu\n", height);
    fprintf(fp, "dim: %zu\n", dim);
    fprintf(fp, "ordered: true\n");
    fprintf(fp, "type: %s\n", dst_type);
    fprintf(fp, "version: 1\n");
    fprintf(fp, "<>\n");
    fclose(fp);

    // Reopen in binary append mode for data
    fp = fopen(filename, "ab");
    if (!fp) return 1;

    size_t total_points = width * height * dim;
    int status = vs__vcps_write_binary_data(fp, data, src_type, dst_type, total_points);

    fclose(fp);
    return status;
}

// vol

volume *vs_vol_new(s32 dims[static 3], bool is_zarr, bool is_tif_stack, bool uses_3d_tif, char* cache_dir, u64 vol_id) {
  //only 3d tiff chunks are currently supported
  assert(is_tif_stack && uses_3d_tif);

  volume *ret = malloc(sizeof(volume));
  if (ret == NULL) {
    assert(false);
    return NULL;
  }

  if (cache_dir != NULL) {
    if (vs__mkdir_p(cache_dir)) {
      printf("Could not mkdir %s\n",cache_dir);
      return NULL;
    }
  }

  *ret = (volume){{dims[0], dims[1], dims[2]}, is_zarr, is_tif_stack, uses_3d_tif, cache_dir, vol_id};
  return ret;
}

chunk* vs_vol_get_chunk(volume* vol, s32 chunk_pos[static 3], s32 chunk_dims[static 3]) {
  // stitching across chunks, be them tiff or zarr chunks, isn't just yet supported
  // so for now we'll just force the position to be a multiple of 500
  // and for dims to be <= 500

  assert(chunk_pos[0] % 500 == 0);
  assert(chunk_pos[1] % 500 == 0);
  assert(chunk_pos[2] % 500 == 0);
  assert(chunk_dims[0] <= 500);
  assert(chunk_dims[1] <= 500);
  assert(chunk_dims[2] <= 500);

  int z = chunk_pos[0] / 500;
  int y = chunk_pos[1] / 500;
  int x = chunk_pos[2] / 500;

  char filename[1024] = {'\0'};
  sprintf(filename, "cell_yxz_%03d_%03d_%03d.tif",y,x,z);
  char filepath[1024] = {'\0'};
  sprintf(filepath, "%s/%s",vol->cache_dir, filename);

  if (vs__path_exists(filepath)) {
      chunk* ret = vs_tiff_to_chunk(filepath);
      return ret;
  } else {
#ifdef VESUVIUS_CURL_IMPL
      char url[1024] = {'\0'};
      void* buf;

      sprintf(url, "https://dl.ash2txt.org/full-scrolls/Scroll1/PHercParis4.volpkg/volume_grids/20230205180739/%s",filename);

      printf("downloading data from %s\n",url);

      long len = vs_download(url, &buf);
      if (len < 0) {
          // download failed
          return NULL;
      }
      printf("len %d\n",(s32)len);
      //todo: is this applicable for all of our 3d tiff files from the data server?
      if (len != 250073508 ) {
          printf("warning! did not download the seemingly correct length from %s\n",url);
      }

      if (vol->cache_dir != NULL) {

          printf("saving data to %s\n",filepath);
          FILE* fp = fopen(filepath, "wb");
          if (fp == NULL) {
              printf("could not open %s for writing\n",filepath);
              return NULL;
          }
          size_t sz = fwrite(buf, 1, len, fp);
          printf("wrote: %lld bytes to %s\n",sz, filepath);
          if (sz != len) {
              printf("could not write all data to %s\n",filepath);
          }
      }
#endif
  }
  return NULL;
}

        
// zarr

#ifdef VESUVIUS_ZARR_IMPL

static struct json_value_s *vs__json_find_value(const struct json_object_s *obj, const char *key) {
  struct json_object_element_s *element = obj->start;
  while (element) {
    if (element->name->string_size == strlen(key) &&
        strncmp(element->name->string, key, element->name->string_size) == 0) {
      return element->value;
    }
    element = element->next;
  }
  return NULL;
}

static void vs__json_parse_int32_array(struct json_array_s *array, int32_t output[3]) {
  struct json_array_element_s *element = array->start;
  for (int i = 0; i < 3 && element; i++) {
    struct json_number_s *num = element->value->payload;
    output[i] = (int32_t) strtol(num->number, NULL, 10);
    element = element->next;
  }
}

static int vs__zarr_parse_metadata(const char *json_string, zarr_metadata *metadata) {
  struct json_value_s *root = json_parse(json_string, strlen(json_string));
  if (!root) {
    printf("Failed to parse JSON!\n");
    return 0;
  }

  struct json_object_s *object = root->payload;

  struct json_value_s *shapes_value = vs__json_find_value(object, "shape");
  if (shapes_value && shapes_value->type == json_type_array) {
    vs__json_parse_int32_array(shapes_value->payload, metadata->shape);
  }

  struct json_value_s *chunks_value = vs__json_find_value(object, "chunks");
  if (chunks_value && chunks_value->type == json_type_array) {
    vs__json_parse_int32_array(chunks_value->payload, metadata->chunks);
  }

  struct json_value_s *compressor_value = vs__json_find_value(object, "compressor");
  if (compressor_value && compressor_value->type == json_type_object) {
    struct json_object_s *compressor = compressor_value->payload;

    struct json_value_s *blocksize = vs__json_find_value(compressor, "blocksize");
    if (blocksize && blocksize->type == json_type_number) {
      struct json_number_s *num = blocksize->payload;
      metadata->compressor.blocksize = (int32_t) strtol(num->number, NULL, 10);
    }

    struct json_value_s *clevel = vs__json_find_value(compressor, "clevel");
    if (clevel && clevel->type == json_type_number) {
      struct json_number_s *num = clevel->payload;
      metadata->compressor.clevel = (int32_t) strtol(num->number, NULL, 10);
    }

    struct json_value_s *cname = vs__json_find_value(compressor, "cname");
    if (cname && cname->type == json_type_string) {
      struct json_string_s *str = cname->payload;
      strncpy(metadata->compressor.cname, str->string, sizeof(metadata->compressor.cname) - 1);
    }

    struct json_value_s *id = vs__json_find_value(compressor, "id");
    if (id && id->type == json_type_string) {
      struct json_string_s *str = id->payload;
      strncpy(metadata->compressor.id, str->string, sizeof(metadata->compressor.id) - 1);
    }

    struct json_value_s *shuffle = vs__json_find_value(compressor, "shuffle");
    if (shuffle && shuffle->type == json_type_number) {
      struct json_number_s *num = shuffle->payload;
      metadata->compressor.shuffle = (int32_t) strtol(num->number, NULL, 10);
    }
  }

  struct json_value_s *dtype_value = vs__json_find_value(object, "dtype");
  if (dtype_value && dtype_value->type == json_type_string) {
    struct json_string_s *str = dtype_value->payload;
    strncpy(metadata->dtype, str->string, sizeof(metadata->dtype) - 1);
  }

  struct json_value_s *fill_value = vs__json_find_value(object, "fill_value");
  if (fill_value && fill_value->type == json_type_number) {
    struct json_number_s *num = fill_value->payload;
    metadata->fill_value = (int32_t) strtol(num->number, NULL, 10);
  }

  struct json_value_s *order_value = vs__json_find_value(object, "order");
  if (order_value && order_value->type == json_type_string) {
    struct json_string_s *str = order_value->payload;
    metadata->order = str->string[0];
  }

  struct json_value_s *format_value = vs__json_find_value(object, "zarr_format");
  if (format_value && format_value->type == json_type_number) {
    struct json_number_s *num = format_value->payload;
    metadata->zarr_format = (int32_t) strtol(num->number, NULL, 10);
  }

  free(root);
  return 1;
}

zarr_metadata vs_zarr_parse_zarray(char *path) {
  zarr_metadata metadata = {0};

  FILE *fp = fopen(path, "rt");
  if (fp == NULL) {
    printf("could not open file %s\n", path);
    assert(false);
    return metadata;
  }
  s32 size;
  fseek(fp, 0, SEEK_END);
  size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *buf = calloc(size + 1, 1);
  fread(buf, 1, size, fp);


  if (vs__zarr_parse_metadata(buf, &metadata)) {
    printf("Shape: [%d, %d, %d]\n",
           metadata.shape[0], metadata.shape[1], metadata.shape[2]);
    printf("Chunks: [%d, %d, %d]\n",
           metadata.chunks[0], metadata.chunks[1], metadata.chunks[2]);
    printf("Compressor:\n");
    printf("  blocksize: %d\n", metadata.compressor.blocksize);
    printf("  clevel: %d\n", metadata.compressor.clevel);
    printf("  cname: %s\n", metadata.compressor.cname);
    printf("  id: %s\n", metadata.compressor.id);
    printf("  shuffle: %d\n", metadata.compressor.shuffle);
    printf("dtype: %s\n", metadata.dtype);
    printf("fill_value: %d\n", metadata.fill_value);
    printf("order: %c\n", metadata.order);
    printf("zarr_format: %d\n", metadata.zarr_format);
  }

  free(buf);
  return metadata;
}
#endif

//vesuvius specific
chunk *vs_tiff_to_chunk(const char *tiffpath) {
  TiffImage *img = vs_tiff_read(tiffpath);
  if (!img || !img->isValid) {
    assert(false);
    return NULL;
  }
  if (img->depth <= 1) {
    printf("can't load a 2d tiff as a chunk");
    assert(false);
    return NULL;
  }

  //TODO: can we assume that all 3D tiffs have the same x,y dimensions for all slices? because we are right here
  s32 dims[3] = {img->depth, img->directories[0].height, img->directories[0].width};
  chunk *ret = vs_chunk_new(dims);
  for (s32 z = 0; z < dims[0]; z++) {
    void *buf = vs_tiff_read_directory_data(img, z);
    for (s32 y = 0; y < dims[1]; y++) {
      for (s32 x = 0; x < dims[2]; x++) {
        if (img->directories[z].bitsPerSample == 8) {
          ret->data[z * dims[1] * dims[2] + y * dims[2] + x] = vs_tiff_pixel8(
            buf, y, x, img->directories[z].width);
        } else if (img->directories[z].bitsPerSample == 16) {
          ret->data[z * dims[1] * dims[2] + y * dims[2] + x] = vs_tiff_pixel16(
            buf, y, x, img->directories[z].width);
        }
      }
    }
  }
  return ret;
}


slice *vs_tiff_to_slice(const char *tiffpath, int index) {
  TiffImage *img = vs_tiff_read(tiffpath);
  if (!img || !img->isValid) {
    assert(false);
    return NULL;
  }
  if (index < 0 || index >= img->depth) {
    assert(false);
    return NULL;
  }

  s32 dims[2] = {img->directories[0].height, img->directories[0].width};
  slice *ret = vs_slice_new(dims);

  void *buf = vs_tiff_read_directory_data(img, index);
  for (s32 y = 0; y < dims[0]; y++) {
    for (s32 x = 0; x < dims[1]; x++) {
      if (img->directories[index].bitsPerSample == 8) {
        ret->data[y * dims[1] + x] = vs_tiff_pixel8(buf, y, x, img->directories[index].width);
      } else if (img->directories[index].bitsPerSample == 16) {
        ret->data[y * dims[1] + x] = vs_tiff_pixel16(buf, y, x, img->directories[index].width);
      }
    }
  }
  return ret;
}



int vs_slice_fill(slice *slice, volume *vol, int start[static 2], int axis) {
  assert(axis == 'z' || axis == 'y' || axis == 'x');
  if (start[0] + slice->dims[0] < 0 || start[0] + slice->dims[0] > vol->dims[0]) {
    assert(false);
    return 1;
  }
  if (start[1] + slice->dims[1] < 0 || start[1] + slice->dims[1] > vol->dims[1]) {
    assert(false);
    return 1;
  }

  for (int y = 0; y < vol->dims[0]; y++) {
    for (int x = 0; x < vol->dims[1]; x++) {
      //TODO: actually get the data
      slice->data[y * slice->dims[1] + x] = 0.0f;
    }
  }
  return 0;
}

int vs_chunk_fill(chunk *chunk, volume *vol, int start[static 3]) {
  if (start[0] + chunk->dims[0] < 0 || start[0] + chunk->dims[0] > vol->dims[0]) {
    assert(false);
    return 1;
  }
  if (start[1] + chunk->dims[1] < 0 || start[1] + chunk->dims[1] > vol->dims[1]) {
    assert(false);
    return 1;
  }
  if (start[2] + chunk->dims[2] < 0 || start[2] + chunk->dims[2] > vol->dims[2]) {
    assert(false);
    return 1;
  }

  for (int z = 0; z < vol->dims[0]; z++) {
    for (int y = 0; y < vol->dims[1]; y++) {
      for (int x = 0; x < vol->dims[2]; x++) {
        //TODO: actually get the data
        chunk->data[z * chunk->dims[1] * chunk->dims[2] + y * chunk->dims[2] + x] = 0.0f;
      }
    }
  }
  return 0;
}



#endif // defined(VESUVIUS_IMPL)
#endif // VESUVIUS_H
