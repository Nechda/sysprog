#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <unistd.h>


#include "Types.h"
#include "AssertAddr.h"
#include "Parser.h"
#include "Ringbuffer.h"
#include "Execution.h"


ui32 countBackGroundProcess = 0;
ui32 countZombies = 0;
struct Pipe zombiePipe;


/**
    \brief  Обработчик сигнала SIGCHLD
*/
static void child_handler(int sig)
{
    pid_t pid = 0;
    int status = 0;

    while((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        countZombies++;
        if(write(zombiePipe.writeDesc, &pid, sizeof(pid)) == -1)
            handle_error("Can`t write into zombie pipe.");
    }
}

/**
    \brief  Функция ловит все зомби процессы
*/
void killZombies()
{
    while(countZombies > 0)
    {
        pid_t pid = 0;
        if(read(zombiePipe.readDesc, &pid, sizeof(pid)) != sizeof(pid))
            handle_error("We have read broken data from zombie pipe.");
        ringBufferDelItem(pid);
        countZombies--;
        if(countBackGroundProcess) countBackGroundProcess--;
    }
}


int main(int argc, char** argv)
{
    if(pipe((int*)&zombiePipe) < 0)
        handle_error("Can`t create zombie pipe.");

    struct sigaction sa;
    sa.sa_handler = &child_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, 0) == -1)
        handle_error("Can`t set SIGCHLD.");

    ringBufferInit();
    while(1)
    {
        struct Task* newTask = parseLine();
        Assert_addr(newTask);
        if(newTask->isBackGround)
        {
            pid_t pid = fork();
            if(pid == 0)
            {
                executeTask(newTask);
                exit(EXIT_SUCCESS);
            }
            if(pid < 0)
                printf("There are an erro related to fork in main()\n");
            countBackGroundProcess++;
            ringBufferPushItem(pid, newTask);
            continue;
        }

        executeTask(newTask);
        cleanUpTask(newTask);
        killZombies();
        if(getIsEndOfFile())
            break;
    }

    while(countBackGroundProcess)
        killZombies();
    ringBufferCleanUp();
    close(zombiePipe.readDesc);
    close(zombiePipe.writeDesc);
    return 0;
}
