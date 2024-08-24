#include "vesuvius-c.h"
#include <stdio.h>


int main() {
    // Initialize the library
    init_vesuvius();

    // Read a single value from the volume
    unsigned char value;
    int x = 3693, y = 2881, z = 6777;
    if (get_zarr_value(x, y, z, &value) == 0) {
        printf("Value at (%d, %d, %d): %u\n", x, y, z, value);
    }

    // Fill an image slice
    int image_width = 1024, image_height = 1024;
    unsigned char *image = (unsigned char *)malloc(image_width * image_height);

    int x_start = 3200, x_end = x_start + image_width - 1;
    int y_start = 3000, y_end = y_start + image_height - 1;
    int slice_z = 6777;

    if (fill_image_slice(x_start, x_end, y_start, y_end, slice_z, image) == 0) {
        printf("Successfully filled image slice at z=%d\n", slice_z);

        // Write the image slice to a BMP file
        if (write_bmp("sample_image.bmp", image, image_width, image_height) == 0) {
            printf("Successfully wrote image slice to sample_image.bmp\n");
        } else {
            fprintf(stderr, "Failed to write image slice to file\n");
        }
    }

    // Free the image memory
    free(image);

    return 0;
}
