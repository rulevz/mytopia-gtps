// serialize_dump.cpp - dump world::serialize() for a world name so
// tests/world_stream_check.py can verify the client-side parse lands on exact EOF.
//
// build (from the repo root):
//   g++ -std=c++20 -Iinclude -Ibuild/include \
//       -I$HOME/opt/gtps-deps/root/usr/include -I$HOME/opt/gtps-deps/root/usr/include/x86_64-linux-gnu \
//       -include include/pch.hpp tools/gt-re/serialize_dump.cpp $(find build/include -name '*.o') \
//       -o tools/gt-re/serialize_dump \
//       -L./include/enet/lib -L$HOME/opt/gtps-deps/root/usr/lib/x86_64-linux-gnu \
//       -lssl -lcrypto -lmariadb -lenet -lm -lpthread -lz
#include "pch.hpp"

bool g_verbose = false; // @note main.cpp defines this; the dump tool needs its own copy

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <world> <out.bin>\n", argv[0]); return 2; }

    ::mysql_connect();
    ::decode_items(); // @note world::serialize() calls id_to_item() on every tile

    ::world w{ argv[1] };
    ::blob blob = w.serialize();

    /* @note NOT stdout: this binary links the server's logging, which prints to stdout
       and would corrupt the stream. Always write to an explicit file. */
    FILE *f = fopen(argv[2], "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
    fwrite(blob.data().data(), 1, blob.size(), f);
    fclose(f);

    fprintf(stderr, "world=%s bytes=%zu providers=%zu displays=%zu -> %s\n",
        w.name.c_str(), blob.size(), w.providers.size(), w.displays.size(), argv[2]);
    return 0;
}
