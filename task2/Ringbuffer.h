#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "Types.h"
#include "parser.h"
#include <sys/types.h>

void ringBufferInit();
void ringBufferPushItem(pid_t pid, struct Task* task);
void ringBufferDelItem(pid_t pid);
void ringBufferCleanUp();

#endif
