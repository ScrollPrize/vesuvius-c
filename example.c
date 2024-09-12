#include "vesuvius-c.h"
#include <stdio.h>


int main() {
    // Initialize the library
    init_vesuvius();

    // Read a single value from the scroll volume
    unsigned char value;
    int x = 3693, y = 2881, z = 6777;
    if (get_volume_voxel(x, y, z, &value) == 0) {
        printf("Voxel value at (%d, %d, %d): %u\n", x, y, z, value);
    }
    // value <- 83

    // Define a region of interest in the scroll volume
    RegionOfInterest roi = {
        .x_start = 3456, .y_start = 3256, .z_start = 6521,
        .x_width = 256, .y_height = 256, .z_depth = 256,
    };

    // Fetch this region of interest into a local 3D volume
    unsigned char *volume = (unsigned char *)malloc(roi.x_width * roi.y_height * roi.z_depth);
    if (get_volume_roi(roi, volume) == 0) {
        printf("Filled volume ROI: %d+%d, %d+%d, %d+%d\n", roi.x_start, roi.x_width, roi.y_start, roi.y_height, roi.z_start, roi.z_depth);
    }

    // Fetch the same region again (will come from the cache this time)
    unsigned char *volume2 = (unsigned char *)malloc(roi.x_width * roi.y_height * roi.z_depth);
    if (get_volume_roi(roi, volume2) == 0) {
        printf("Filled volume ROI from cache: %d+%d, %d+%d, %d+%d\n", roi.x_start, roi.x_width, roi.y_start, roi.y_height, roi.z_start, roi.z_depth);
    }
    free(volume2);

    // Write the three orthogonal slice planes from the region of interest
    unsigned char *xy_slice = (unsigned char *)malloc(roi.x_width * roi.y_height);
    int middle_z = roi.z_depth / 2;
    for (int y = 0; y < roi.y_height; y++) {
        for (int x = 0; x < roi.x_width; x++) {
            xy_slice[y * roi.x_width + x] = volume[middle_z * roi.x_width * roi.y_height + y * roi.x_width + x];
        }
    }
    write_bmp("xy_slice.bmp", xy_slice, roi.x_width, roi.y_height);
    free(xy_slice);

    unsigned char *xz_slice = (unsigned char *)malloc(roi.x_width * roi.z_depth);
    int middle_y = roi.y_height / 2;
    for (int z = 0; z < roi.z_depth; z++) {
        for (int x = 0; x < roi.x_width; x++) {
            xz_slice[z * roi.x_width + x] = volume[z * roi.y_height * roi.x_width + middle_y * roi.x_width + x];
        }
    }
    write_bmp("xz_slice.bmp", xz_slice, roi.x_width, roi.z_depth);
    free(xz_slice);

    unsigned char *yz_slice = (unsigned char *)malloc(roi.y_height * roi.z_depth);
    int middle_x = roi.x_width / 2;
    for (int z = 0; z < roi.z_depth; z++) {
        for (int y = 0; y < roi.y_height; y++) {
            yz_slice[z * roi.y_height + y] = volume[z * roi.y_height * roi.x_width + y * roi.x_width + middle_x];
        }
    }
    write_bmp("yz_slice.bmp", yz_slice, roi.y_height, roi.z_depth);
    free(yz_slice);

    // Fetch a slice plane from the volume (region of interest with a depth of 1)
    // This is identical to the xy_slice taken from the above region of interest
    unsigned char *slice = (unsigned char *)malloc(roi.x_width * roi.y_height);
    roi.z_start = roi.z_start + middle_z;
    roi.z_depth = 1;
    if (get_volume_slice(roi, slice) == 0) {
        printf("Filled volume slice: %d+%d, %d+%d, %d\n", roi.x_start, roi.x_width, roi.y_start, roi.y_height, roi.z_start);
    }
    write_bmp("slice.bmp", slice, roi.x_width, roi.y_height);
    free(slice);

    free(volume);

    // Fetch an .obj
    TriangleMesh mesh;
    if (get_triangle_mesh("20231005123336", &mesh) == 0) {
        printf("Fetched triangle mesh with %zu vertices and %zu triangles\n", mesh.vertex_count, mesh.triangle_count);
    }

    // Write the triangle mesh to an .obj file
    if (write_trianglemesh_to_obj("mesh.obj", &mesh) == 0) {
        printf("Wrote triangle mesh to mesh.obj\n");
    }

    return 0;
}
