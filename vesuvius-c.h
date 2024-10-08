#ifndef VESUVIUS_H
#define VESUVIUS_H

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <blosc2.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <float.h>

// Zarr config
#define ZARR_URL "https://dl.ash2txt.org/other/dev/scrolls/1/volumes/54keV_7.91um.zarr/0/"
#define CHUNK_SIZE_X 128
#define CHUNK_SIZE_Y 128
#define CHUNK_SIZE_Z 128
#define SHAPE_X 8096
#define SHAPE_Y 7888
#define SHAPE_Z 14376
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
void init_vesuvius();

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

// Initialize the vesuvius library
void init_vesuvius() {
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

#endif // VESUVIUS_H
