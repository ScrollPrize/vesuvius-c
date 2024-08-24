#include "vescuvius.h"
#include <stdio.h>

int main() {
    int x = 3693;
    int y = 2881;
    int z = 6604;
    unsigned char value;

    if (get_zarr_value(x, y, z, &value) == 0) {
        printf("Value at (%d, %d, %d) is: %u\n", x, y, z, value);
    } else {
        fprintf(stderr, "Error retrieving value.\n");
    }

    return 0;
}