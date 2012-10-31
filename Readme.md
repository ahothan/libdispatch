libdispatch for Linux
=====================

[libdispatch](http://libdispatch.macosforge.org), aka Grand Central Dispatch (GCD) is Apple's high-performance event-handling library, introduced in OS X Snow Leopard. It provides a cornucopia of pleasures: asynchronous task queues, monitoring of socket read and write-ability, asynchronous I/O, readers-writer locks, parallel for-loops, sane signal handling, timers, etc etc. Never use pthreads again. 

Currently, the trunk of the [official SVN repository](http://libdispatch.macosforge.org/trac/browser) doesn't build on Linux; the last revision that works out-of-the-box is `r199`, but that revision doesn't contain any of the nifty APIs added in OS X Lion, e.g. asynchronous I/O. This repo applies some patches by Mark Heily, taken from [his post to the libdispatch mailing list](http://lists.macosforge.org/pipermail/libdispatch-dev/2012-August/000676.html), along with some other fixes that I've cobbled together.

I've also added missing `_f` variants for several functions in `data.h` and `io.h` that took [Objective-C blocks](http://developer.apple.com/library/ios/#documentation/cocoa/Conceptual/Blocks/Articles/00_Introduction.html) only: look for the functions with `_np` appended to them. Although you can make full use of libdispatch with a compiler like GCC that don't support blocks, libdispatch itself must be built with Clang, as it makes use of blocks internally.

You'll want to read over Apple's [API reference](http://developer.apple.com/library/ios/#documentation/Performance/Reference/GCD_libdispatch_Ref/Reference/reference.html).

Patches are very welcome; I'm not terribly familiar with Linux.

Prerequisities
--------------
- [libBlocksRuntime](http://mark.heily.com/project/libblocksruntime)
- [libpthread_workqueue](http://mark.heily.com/project/libpthread_workqueue)
- [libkqueue](http://mark.heily.com/project/libkqueue)
- Clang
- automake/autoconf/libtool

How to build
------------
The following does the job on Ubuntu 12.04:

    sudo apt-get install libblocksruntime-dev libkqueue-dev libpthread-workqueue-dev
    git clone git://github.com/nickhutchinson/libdispatch.git && cd libdispatch
    sh autogen.sh
    ./configure CFLAGS="-I/usr/include/kqueue" LDFLAGS="-lkqueue -lpthread_workqueue -pthread -lm"
    make
    sudo make install
    sudo ldconfig

Testing
-------
The included test suite builds and runs on Linux (just run `make check`). Last I checked, a few tests failed:

- dispatch_starfish
- dispatch_vnode

Test failure results are [here](https://gist.github.com/3903724). The dispatch_starfish failure can probably be ignored, but the dispatch_vnode failure seems to indicate some more serious issue, possibly a bug in libkqueue. It might be best to avoid using `dispatch_source_create()` with `DISPATCH_SOURCE_TYPE_VNODE` for now...

Demo
-------
    cat << "EOF" > dispatch_test.c
    #include <dispatch/dispatch.h>
    #include <stdio.h>

    static void timer_did_fire(void *context) { printf("Strawberry fields...\n"); }

    static void write_completion_handler(dispatch_data_t unwritten_data, int error, void *context) {
        if (!unwritten_data && error == 0)
           printf("Dispatch I/O wrote everything to stdout. Hurrah.\n");
    }

    static void read_completion_handler(dispatch_data_t data, int error, void *context) {
        int fd = (intptr_t)context;
        close(fd);
        
        dispatch_write_f_np(STDOUT_FILENO, data, dispatch_get_main_queue(),
                            NULL, write_completion_handler);
    }
     
    int main(int argc, const char *argv[]) {
        dispatch_source_t timer = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());

        dispatch_source_set_event_handler_f(timer, timer_did_fire);
        dispatch_source_set_timer(timer, DISPATCH_TIME_NOW, 1 * NSEC_PER_SEC,
                                  0.5 * NSEC_PER_SEC);
        dispatch_resume(timer);

        int fd = open("dispatch_test.c", O_RDONLY);

        dispatch_read_f_np(fd, SIZE_MAX, dispatch_get_main_queue(), (void *)(intptr_t)fd,
                        read_completion_handler);

        dispatch_main();
        return 0;
    }
    EOF

    clang dispatch_test.c -I/usr/local/include -L/usr/local/lib -ldispatch -o dispatchTest
    ./dispatchTest
