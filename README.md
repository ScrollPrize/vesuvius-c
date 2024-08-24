# vesuvius-c

From [Vesuvius Challenge](https://scrollprize.org), a single-header C library for accessing CT scans of ancient scrolls.

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

Resulting image:

<img src="img/sample_image.png" alt="Example scroll data" width="200"/>

The library fetches scroll data from the Vesuvius Challenge [data server](https://dl.ash2txt.org) in the background. Only the necessary volume chunks are requested, and an in-memory LRU cache holds recent chunks to avoid repeat downloads.

> ⚠️ `vesuvius-c` is in beta and the interface may change. Only Scroll 1 is currently supported. More data may be added in the future.

## Usage

See [example.c](example.c) for example library usage.

## Building

### Dependencies:

* libcurl
* c-blosc2

`libcurl` is used for fetching volume chunks and is likely already available on your system. `c-blosc2` is used to decompress the Zarr chunks read from the server and may require installation.

### Build and run:

Simply link the dependencies and build your program:

```sh
gcc -o example example.c -lcurl -lblosc2
./example
```

It may be necessary to point to the `c-blosc2` installation, for example:

```sh
gcc -o example example.c -I/opt/homebrew/Cellar/c-blosc2/2.15.1/include -L/opt/homebrew/Cellar/c-blosc2/2.15.1/lib -lcurl -lblosc2
./example
```

## Next features

* Volumes in addition to Scroll 1
* Reading scroll segments (`.obj` mesh files)
