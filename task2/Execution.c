#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>



#include "Execution.h"
#include "Parser.h"
#include "AssertAddr.h"

static int currentInFile = STDIN_FILENO;
static int currentOutFile = STDOUT_FILENO;

static ui8 getOutputFileType(struct Command command, enum Divider curDiv)
{
    if(command.argc >= 3 && curDiv != DIV_PIPE)
    {
        ui16 word = command.argv[command.argc - 2][0] << 8 | command.argv[command.argc - 2][1] << 0;
        #define GET_WORD(ch1, ch2) ((ch1 << 8) | (ch2 << 0))
        switch(word)
        {
            case GET_WORD('>','\0'):
                return 1;
            break;
            case GET_WORD('>', '>'):
                return 2;
            break;
            default:
                return -1;
            break;
        }
        #undef GET_WORD
    }
    return -1;
}

/**
    \brief  Функция реализует поведение некоторых стандартных
            команд терминала
    \param  [in]  arc  Количество аргументов командной строки
    \param  [in]  argv Массив строк с аргументами
    \note   Данная функция рализует команды exit, true, false
*/
static void launchCustomUtility(ui32 argc, C_string* argv)
{
    if(!strcmp(argv[0],"exit"))
    {
        ui32 code = 0;
        switch(argc)
        {
            case 1:
                exit(EXIT_SUCCESS);
            break;
            case 2:
                sscanf(argv[1], "%u", &code);
                exit(code);
            break;
            default:
                exit(EXIT_FAILURE);
            break;
        }
    }

    if(!strcmp(argv[0],"true"))
        exit(EXIT_SUCCESS);

    if(!strcmp(argv[0],"false"))
        exit(EXIT_FAILURE);
        
}

/**
    \brief  Функция реализует выполнение команд терминала,
            которые являются сторонними утилитами.
    \param  [in]  command  Структура с информацией о команде и её параметрах
    \param  [in]  infile   Дескриптор файла ввода
    \param  [in]  outfile  Дескриптор файла вывода
    \param  [in]  errile   Дескриптор файла ошибок
*/
static void launchProcess(struct Command command, int infile, int outfile, int errfile)
{
    if(infile != STDIN_FILENO)
    {
        dup2(infile, STDIN_FILENO);
        close(infile);
    }

    if(outfile != STDOUT_FILENO)
    {
        dup2(outfile, STDOUT_FILENO);
        close(outfile);
    }

    if(errfile != STDERR_FILENO)
    {
        dup2(errfile, STDERR_FILENO);
        close(errfile);
    }

    bool isFile = false;

    launchCustomUtility(command.argc, command.argv);

    C_string* argv = calloc(command.argc + 1 - 2 * isFile, sizeof(C_string));
    Assert_addr(argv);
    memcpy(argv, command.argv, sizeof(C_string) * (command.argc - 2 * isFile));
    execvp(argv[0], argv);
    perror("execvp");
    exit(1);
}

/**
    \brief  Функция выполняет команду, настраивая дескрипторы ввода и вывода
    \param  [in]  command  Структура с информацией о команде и её параметрах
    \param  [in]  curDiv   Текущий разделитель
    \param  [in]  prevDiv  Предыдущий разделитель
    \return Код, с которым вернулася исполняемый процесс
*/
static int doCommand(struct Command command, enum Divider curDiv, enum Divider prevDiv)
{

    if(!strcmp(command.argv[0],"cd"))
    {
        if(command.argc == 2)
        {
            DIR* dir = opendir(command.argv[1]);
            if(ENOENT == errno && dir == 0)
                printf("`%s` directory does not exist.\n", command.argv[1]);
            else
            if ( chdir(command.argv[1]) )
                handle_error("Can`t change directory.");
            closedir(dir);
            return 0;
        }
        else
            printf("Invalid data format for cd command.\n");
    }


    if(!strcmp(command.argv[0],"exit") && prevDiv == DIV_NONE && curDiv == DIV_NONE)
        exit(EXIT_SUCCESS);
    

    if(prevDiv != DIV_PIPE)
        currentInFile = STDIN_FILENO;
    if(curDiv != DIV_PIPE)
        currentOutFile = STDOUT_FILENO;


    {
        //how many `>` in string, if there arent any `>` then it`s -1
        i8 outFileType = getOutputFileType(command, curDiv); 
        if(outFileType != -1)
        {
            int flags = O_CREAT | O_RDWR | ( outFileType == 2 ? O_APPEND : O_TRUNC);
            currentOutFile = open(command.argv[command.argc - 1], flags , S_IRUSR | S_IWUSR);
            free(command.argv[command.argc - 2]);
            command.argv[command.argc - 2] = NULL;
        }
    }

    

    struct Pipe mypipe;    
    if(curDiv == DIV_PIPE)
    {
        if(pipe((int*)&mypipe) < 0)
            handle_error("Can`t create zombie pipe.");
        currentOutFile = mypipe.writeDesc;
    }

    pid_t pid = fork();
    if(!pid)
        launchProcess(command, currentInFile, currentOutFile, STDERR_FILENO); // do some task in child
    int stat;
    wait(&stat);

    //close file desc
    if( currentInFile != STDIN_FILENO )
        close(currentInFile);
    if( currentOutFile != STDOUT_FILENO )
        close(currentOutFile);
    currentInFile = mypipe.readDesc;

    return WEXITSTATUS(stat);
}


/**
    \brief  Функция запускает на исполнение набор команд
    \param  [in]  task  Указатель на структуру Task с
                        информацией о наборе команд
*/
void executeTask(struct Task* task)
{
    if(!task)
        return;
    if(!task->commands || (task->nCommands >= 2 && !task->dividers))
        return;
    int exitCode = 0;
    enum Divider curDiv = DIV_NONE;
    enum Divider prevDiv = DIV_NONE;

    ui32 nCommands = task->nCommands;
    struct Command* commands = task->commands;
    enum Divider* dividers = task->dividers;


    bool isErrorOccur = !task->commands->strSizes[0];
    for(ui32 i = 0; i < nCommands && !isErrorOccur; i++)
    {
        curDiv = i == nCommands - 1 ? DIV_NONE : dividers[i];
        exitCode = doCommand(commands[i], curDiv, prevDiv);
        // был оператор && и прошлай команда завершилась с ошибкой
        if(curDiv == DIV_AND && exitCode) 
            break;
        // был оператор OR и у нас получилось выполнить команду
        if(curDiv == DIV_OR && !exitCode) 
            for(; i< nCommands - 1 && dividers[i] == DIV_OR; i++);
        prevDiv = curDiv;
    }
}
