#include "vescuvius.h"
#include <stdio.h>


int main() {
    // Initialize the vescuvius library
    init_vescuvius();

    // Read a single value from the volume
    unsigned char value;
    int x = 3693, y = 2881, z = 6604;
    if (get_zarr_value(x, y, z, &value) == 0) {
        printf("Value at (%d, %d, %d): %u\n", x, y, z, value);
    }

    // Fill an image slice
    int image_width = 1024, image_height = 1024;
    unsigned char *image = (unsigned char *)malloc(image_width * image_height);

    // Fill the image slice at z=256
    int slice_z = 256;
    if (fill_image_slice(slice_z, image, image_width, image_height) == 0) {
        printf("Successfully filled image slice at z=%d\n", slice_z);

        // Write the image slice to a BMP file
        if (write_bmp("slice_z_256.bmp", image, image_width, image_height) == 0) {
            printf("Successfully wrote image slice to slice_z_256.bmp\n");
        } else {
            fprintf(stderr, "Failed to write image slice to file\n");
        }
    }

    // Free the image memory
    free(image);

    return 0;
}
