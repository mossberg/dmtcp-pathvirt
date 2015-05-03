/* NOTE:  if you just want to insert your own code at the time of checkpoint
 *  and restart, there are two simpler additional mechanisms:
 *  dmtcpaware, and the MTCP special hook functions:
 *    mtcpHookPreCheckpoint, mtcpHookPostCheckpoint, mtcpHookRestart
 */

#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include "dmtcp.h"

// always include trailing /
const char* oldprefix="/home/markmoss/dmtcp/test/plugin/pathvirt/slot5/";
const char* newprefix="/home/markmoss/dmtcp/test/plugin/pathvirt/misc/slot7/";

void print_time() {
  struct timeval val;
  gettimeofday(&val, NULL);
  printf("%ld %ld", (long)val.tv_sec, (long)val.tv_usec);
}

/* unsigned int sleep(unsigned int seconds) { */
/*   printf("sleep1: "); print_time(); printf(" ... "); */
/*   unsigned int result = NEXT_FNC(sleep)(seconds); */
/*   print_time(); printf("\n"); */

/*   return result; */
/* } */

static int startswith(const char *target, const char *prefix) {
    // invert strncmp ret to return true if startswith and false else
    return !strncmp(target, prefix, strlen(prefix));
}


int open(const char *path, int oflag, mode_t mode) {
    printf("WRAPPED OPEN2!\n");
    if (startswith(path, oldprefix)) {
        puts("old path prefix detected. swapping");
        size_t newpathsize = (strlen(path) - strlen(oldprefix)) + strlen(newprefix);
        char newpath[newpathsize + 1];
        snprintf(newpath, sizeof newpath, "%s%s", newprefix,
                 path + strlen(oldprefix));
        return NEXT_FNC(open)(newpath, oflag, mode);
    } else {
        return NEXT_FNC(open)(path, oflag, mode);
    }
}

void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_WRITE_CKPT:
    printf("\n*** The plugin %s is being called before checkpointing. ***\n",
	   __FILE__);
    break;
  case DMTCP_EVENT_RESUME:
    printf("*** The plugin %s has now been checkpointed. ***\n", __FILE__);
    break;
  default:
    ;
  }

  /* Call this next line in order to pass DMTCP events to later plugins. */
  DMTCP_NEXT_EVENT_HOOK(event, data);
}
