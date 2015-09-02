/* NOTE:  if you just want to insert your own code at the time of checkpoint
 *  and restart, there are two simpler additional mechanisms:
 *  dmtcpaware, and the MTCP special hook functions:
 *    mtcpHookPreCheckpoint, mtcpHookPostCheckpoint, mtcpHookRestart
 */

#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include "dmtcp.h"

#define ENV_DPP "DMTCP_PATH_PREFIX"

/* paths should only be swapped on restarts (not on initial run), so this flag */
/* is set on restart */
static int should_swap;

// NOTE: DMTCP_PATH_PREFIX env variables cannot exceed 1024 characters in length
// TODO: dynamically allocate
static char old_path_prefix_list[1024];
static char new_path_prefix_list[1024];

/*
 * Helper Functions
 */

/*
 * clfind - returns first index in colonlist which is a prefix for path
 */
static int clfind(char *colonlist, const char *path)
{
    int index = 0;
    char *element = colonlist, *colon;

    /* while there is a colon present, loop */
    while (colon = strchr(element, ':')) {
        /* check if element is a prefix of path. here, colon - element is
           an easy way to calculate the length of the element in the list
           to use as the size parameter to strncmp */
        if (strncmp(path, element, colon - element) == 0)
            return index;

        /* move element to point to next element */
        element = colon + 1;

        /* bump index count */
        index++;
    }

    /* process the last element in the list */
    if (strncmp(path, element, strlen(element)) == 0)
        return index;

    /* not found */
    return -1;
}

/*
 * clget - returns pointer to element in colonlist at index i
 *         and NULL if not found
 */
char *clget(char *colonlist, unsigned int i)
{
    int curr_ind = 0;
    char *element = colonlist, *colon;

    /* iterate through elements until last one */
    while (colon = strchr(element, ':')) {
        /* if we are at the request index, return pointer to start of element */
        if (curr_ind == i)
            return element;

        /* otherwise, advance pointer to next element and bump current index */
        element = colon + 1;
        curr_ind++;
    }

    /* last element */
    if (curr_ind == i)
        return element;

    /* not found */
    return NULL;
}

/*
 * clgetsize_ptr - returns size of an element pointed to by @element in the
 *                 list
 */
static size_t clgetsize_ptr(char *colonlist, char *element)
{
    /* either calculate the element's length, or call
     * strlen if element was last one */
    char *colon = strchr(element, ':');
    return colon ? colon - element : strlen(element);
}

/*
 * clgetsize - returns size of an element at index i in colonlist
 *             and -1 if not found
 */
static ssize_t clgetsize_ind(char *colonlist, const unsigned int i)
{
    /* get pointer to element at index i */
    char *element = clget(colonlist, i);
    if (element) {
        /* now that we have a pointer, we can use clgetsize_ptr */
        return clgetsize_ptr(colonlist, element);
    }

    /* not found */
    return -1;
}

/*
 * dynamic_path_swap - given old path, return new path or NULL
 *
 * Returns NULL if no swap is to be done and the original path value should
 * be used. Returns a malloc'd pointer to the new string if a swap should
 * happen.
 *
 * If didn't return NULL, the returned pointer must be freed.
 */
static char *dynamic_path_swap(const char *path) 
{
    /* quickly return NULL if no swap */
    if (!should_swap) {
        return NULL;
    }

    /* yes, should swap */

    /* check if path is in list of registered paths to swap out */
    int index = clfind(old_path_prefix_list, path);
    if (index == -1)
        return NULL;

    /* found it in old list, now get a pointer to the new prefix to swap in*/
    char *new = clget(new_path_prefix_list, index);
    if (new == NULL)
        return NULL;

    /* get lengths of old and new prefixes */
    size_t new_element_sz = clgetsize_ptr(new_path_prefix_list, new);
    size_t old_element_sz = clgetsize_ind(old_path_prefix_list, index);

    /* copy new prefix out of new colonlist into our own buffer */
    char newcpy[new_element_sz + 1];
    memcpy(newcpy, new, new_element_sz);
    newcpy[new_element_sz] = '\0';

    /* finally, create full path with the new prefix swapped in */

    /* plus 1 is for safety slash we include between the new prefix and the
       unchanged rest of the path. this is in case their environment
       variable doesn't end with a slash. in the "worst" case,
       there will be two extra slashes if the new prefix ends with a slash
       and the old one doesn't. plus 1 for NULL */
    size_t newpathsize = (strlen(path) - old_element_sz) + strlen(newcpy) + 1 + 1;
    char *newpath = malloc(newpathsize);
    snprintf(newpath, newpathsize, "%s/%s", newcpy, path + old_element_sz);

    return newpath;
}

/*
 * Libc Hooks (for all path related functions)
 */

int fopen64(const char *path, const char *mode)
{
    char *hook_path = dynamic_path_swap(path);

    /* hook_path was NULL, not swapping */
    if (!hook_path)
        return NEXT_FNC(fopen64)(path, mode);

    /* swapping */
    int fd = NEXT_FNC(fopen64)(hook_path, mode);

    /* dynamic_path_swap's return val needs to be free'd */
    free(hook_path);

    return fd;
}

int open(const char *path, int oflag, mode_t mode)
{
    char *hook_path = dynamic_path_swap(path);

    /* hook_path was NULL, not swapping */
    if (!hook_path)
        return NEXT_FNC(open)(path, oflag, mode);

    /* swapping */
    int fd = NEXT_FNC(open)(hook_path, oflag, mode);

    /* dynamic_path_swap's return val needs to be free'd */
    free(hook_path);

    return fd;
}

/*
 * DMTCP Setup
 */

void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
    /* NOTE:  See warning in plugin/README about calls to printf here. */
    switch (event) {
    case DMTCP_EVENT_INIT:
    {
        /* On init, check if they've specified paths to virtualize via
           DMTCP_PATH_PREFIX env */
        char *old_env = getenv(ENV_DPP);
        if (old_env) {
            /* if so, save it to buffer */
            snprintf(old_path_prefix_list, sizeof(old_path_prefix_list), "%s",
                     old_env);
        }

        break;
    }
    case DMTCP_EVENT_RESTART:
    {
        /* necessary since we don't know how many bytes dmtcp_get_restart_env
           will write */
        memset(new_path_prefix_list, 0, sizeof new_path_prefix_list);

        /* Try to get the value of ENV_DPP from new environment variables,
         * passed in on restart */
        int ret = dmtcp_get_restart_env(ENV_DPP, new_path_prefix_list,
                                        sizeof(new_path_prefix_list) - 1);
        if (ret == -1) {
            /* env var did not exist. no new prefix given, so do nothing */
            break;
        } else if (ret == -2) {
            // TODO
            /* need to allocate more memory and retry */
        }

        /* check if an initial DMTCP_PATH_PREFIX was even supplied
         * (old_path_prefix is initialized to zeros on start, and would only
         * contain something if something had been written there */
        if (*old_path_prefix_list)
            should_swap = 1;

        /* if we get here, no old path was given, so do nothing */
        break;
    }

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
