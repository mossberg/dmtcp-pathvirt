#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <err.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, const char *argv[])
{
    /* get cwd */
    /* save to variable */
    /* that variable is used all throughout */

    int count = 0;
    char *append = "appending";
    char *fname = "/pvtest.txt";
    char cwd[128];
    if (!getcwd(cwd, sizeof cwd))
        err(1, "could not get cwd");

    size_t fullpathsize = strlen(cwd) + strlen(fname) + 1;
    char fullpath[fullpathsize];
    snprintf(fullpath, sizeof(fullpath), "%s%s", cwd, fname);

    while (1) {
        printf("[%d] Appending...\n", count);

        int fd = open(fullpath, O_RDWR | O_APPEND | O_CREAT, S_IRWXU);
        if (fd < 0) {
            err(1, "could not open/create file");
        } else {
            dprintf(fd, "%d %s\n", count++, append);
        }

        sleep(1);
    }


    free(cwd);
    return 0;
}