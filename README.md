# vesuvius-c

From [Vesuvius Challenge](https://scrollprize.org), a C library for accessing CT scans of ancient scrolls.

`vesuvius-c` allows direct access to scroll data **without** managing download scripts or storing terabytes of CT scans locally:

```c
#include "vesuvius-c.h"

int main() {
    init_vesuvius();

    // Read an 8-bit value from the 3D scroll volume
    int x = 3693, y = 2881, z = 6777;
    unsigned char value;
    get_intensity(x, y, z, &value);
    // value <- 83

    // Fill an image with a slice of the scroll volume
    // ... init some values ...
    fill_image_slice(x_start, x_end, y_start, y_end, slice_z, image);
    // image <- scroll data!

    // Write image to disk
    write_bmp("sample_image.bmp", image, image_width, image_height);
}
```

The library fetches scroll data from the Vesuvius Challenge [data server](https://dl.ash2txt.org) in the background. Only the necessary volume chunks are requested, and an in-memory LRU cache holds recent chunks to avoid repeat downloads.

> ⚠️ `vesuvius-c` is in beta and the interface may change. Only Scroll 1 is currently supported. More data may be added in the future.

usage

building

dependencies

license