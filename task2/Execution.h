#ifndef EXECUTION_H
#define EXECUTION_H

#include "parser.h"

#define handle_error(msg)\
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

struct Pipe
{
    int readDesc;
    int writeDesc;
};

void executeTask(struct Task* task);

#endif
