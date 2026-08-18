#include "storage/pager.h"
#include <cstdio>
#include <cstring>

int main() {
    const char* filename = "test.db";

    // ---- Part 1: write some data and let the Pager go out of scope ----
    {
        Pager pager(filename);

        PageId p0 = pager.allocate_page();
        const char* message = "Hello, database!";
        // Write the message's raw bytes into the start of page 0's buffer.
        std::memcpy(pager.get_page(p0)->data.data(), message, strlen(message) + 1);
        pager.mark_dirty(p0);

        printf("Wrote to page %u: \"%s\"\n", p0, message);

        // pager's destructor runs here (end of this scope) and flushes
        // to disk automatically — simulating a clean shutdown.
    }

    // ---- Part 2: open a BRAND NEW Pager on the same file ----
    // This proves the data actually made it to disk, not just RAM,
    // because this is a completely fresh object with an empty cache.
    {
        Pager pager(filename);
        Page* page = pager.get_page(0);
        printf("Read back from page 0 after reopening: \"%s\"\n",
               reinterpret_cast<char*>(page->data.data()));
    }

    return 0;
}
