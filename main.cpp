
#include <fcntl.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <unistd.h>
#include "window.h"

int main(int argc, char *argv[])
{
    Window window(600, 480);
    window.EventLoop();
    return 0;
}
