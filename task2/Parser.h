#ifndef PARSER_H
#define PARSER_H

#include "Types.h"
#include <stdbool.h>


struct Command
{
    ui32 argc;
    C_string* argv;
    ui32* strSizes;
};

enum Divider
{
    DIV_NONE,
    DIV_PIPE,
    DIV_AND,
    DIV_OR
};


struct Task
{
    ui32 nCommands;
    struct Command* commands;
    enum Divider* dividers;
    bool isBackGround;
};

bool parseLine(struct Task** ptrToTask); 
bool getIsEndOfFile();
void cleanUpTask(struct Task* task);

#endif
