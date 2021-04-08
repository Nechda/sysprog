#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include "Parser.h"
#include "AssertAddr.h"

static struct Task localTask = {0, 0, 0, 0};

static bool isEndOfFile = false;
static bool isStringMode = false;
static struct Command localCommand = { 0, 0, 0 };


/**
    \berief  Функция в массив параметров команды новый элемент.
    \note    Если у команды изначально не было параметров (первый вызов)
             то выделяется память с запасом.
             Изменяется структура localCommand.
*/
static void pushParam()
{ 
    localCommand.argc++;
    if(localCommand.argv)
        localCommand.argv = realloc(localCommand.argv, sizeof(C_string) * (localCommand.argc + 1));
    else
        localCommand.argv = calloc(2, sizeof(C_string));
    Assert_addr(localCommand.argv);

    if(localCommand.strSizes)
        localCommand.strSizes = realloc(localCommand.strSizes, sizeof(ui32) * (localCommand.argc + 1));
    else
        localCommand.strSizes = calloc(2, sizeof(ui32));
    Assert_addr(localCommand.strSizes);

    localCommand.strSizes[localCommand.argc - 1] = 0;
    localCommand.argv[localCommand.argc - 1] =  calloc(2, sizeof(char));
    Assert_addr(localCommand.argv[localCommand.argc - 1]);
}


/**
    \brief  Функция добавляет символ к текущему параметру команды.
    \param  [in]  ch  Символ, который будет добавляться к параметру.
    \note   Если у текущей команды не было параметров (первый вызов),
            то вызывается pushParam().
*/
static void pushSymbolInParam(char ch)
{ 
    if(localCommand.argc == 0)
        pushParam();
    ui32 pos = localCommand.strSizes[localCommand.argc - 1];
    localCommand.argv[localCommand.argc - 1] =  realloc(localCommand.argv[localCommand.argc - 1], pos + 2);
    Assert_addr(localCommand.argv[localCommand.argc - 1]);
    localCommand.argv[localCommand.argc - 1][pos] = ch;
    localCommand.argv[localCommand.argc - 1][pos + 1] = 0;
    
    localCommand.strSizes[localCommand.argc - 1]++;
}


/**
    \brief  Функция перемещает содержимое временного объекта localCommand
            в конец массива команд.
    \note   После перемещения поля структуры localCommand затираются нулями.
*/
static void pushCommand()
{ 
    //проверяем пустая ли строка в последнем аргументе команды
    if(!localCommand.strSizes)
        return;
    if(localCommand.strSizes[localCommand.argc - 1] == 0)
    {
        localCommand.argc--;
        // тут даже при пустой строке будет лежать 2 нулевых символа
        free(localCommand.argv[localCommand.argc]); 
        localCommand.argv[localCommand.argc] = NULL;
    }

    localTask.nCommands++;
    if(localTask.nCommands > 1)
        localTask.commands = realloc(localTask.commands, sizeof(struct Command) * (localTask.nCommands+1));
    else
        localTask.commands = calloc(1, sizeof(struct Command));
    Assert_addr(localTask.commands);

    //перемещаем данные
    memcpy(&localTask.commands[localTask.nCommands - 1], &localCommand, sizeof(struct Command));
    memset(&localCommand, 0x00, sizeof(struct Command));
}

/**
    \brief  Функция добавляет новый разделитель в конец массива разделителей.
    \param  [in]  divider  Разделитель, который записывается в массив
*/
static void pushDivider(enum Divider divider)
{ 
    if(localTask.dividers)
        localTask.dividers = realloc(localTask.dividers, sizeof(enum Divider) * (localTask.nCommands + 1));
    else
        localTask.dividers = calloc(1, sizeof(enum Divider) * 2);
    Assert_addr(localTask.dividers);
    localTask.dividers[localTask.nCommands - 1] = divider;
    localTask.dividers[localTask.nCommands] = DIV_NONE;
}


/**
    \brief   Функция анализирует символ, который был в нее передан.
    \details Функция анализирует символ, который был в нее передан,
             в зависимости от комбинаций введенных символов выбирается
             тот или иной способ обработки данных, читаемых из stdin.
    
*/
static void characherAnalysis(char ch)
{ 
    static char qmark = 0;
    static bool wasSpace = 1;

    //обработка символов, начинающихся с `\`
    if(ch == '\\')
    {
        ch = getchar();
        if( ch != '\n')
            pushSymbolInParam(ch);
        return;
    }

    //одиночный пробел разделяет параметры команды
    if(ch == ' ' && !isStringMode && !wasSpace)
    {
        pushParam();
        wasSpace = 1;
        return;
    }

    //игнорирование двойных пробелов
    if(ch == ' ' && wasSpace)
        return;

    //в строковом режиме ввода можно в двойных кавычках
    //писать одинарные кавычки без дополнительного символа `\`
    if(ch == '\'' && isStringMode && qmark == '\"')
    {
        pushSymbolInParam(ch);
        return;
    }

    //проверки на конец и начало строковоро режима ввода
    #define isQmark(c) ( c == '\"' || c == '\'')
    if(!isStringMode && isQmark(ch))
    {
        isStringMode = 1;
        qmark = ch;
        return;
    }
    if(isStringMode && qmark == ch)
    {
        isStringMode = 0;
        qmark = 0;
        return;
    }
    #undef isQmark

    
    //проверка наличия разделителя | (pipe) и || (or)
    if(ch == '|')
    {
        char next = getchar();
        if(next != '|')
            ungetc(next, stdin);
        pushCommand();
        pushDivider(next == '|' ? DIV_OR : DIV_PIPE);
        return;
    }

    //проверка наличия разделителя && (and) и оператора &
    //для задания работы в фоновом процессе
    if(ch == '&')
    {
        char next = getchar();
        if(next != '&')
        {
            localTask.isBackGround = 1;
            ungetc(next, stdin);
               return;
        }
        pushCommand();
        pushDivider(DIV_AND);
        return;
    }

    //учет двойных пробелов НЕ в строковом режиме
    wasSpace = ch == ' ' && !isStringMode;
    pushSymbolInParam(ch);
}


/**
    \brief  Функция парсит stdin, генерирует информацию
            о задании, которое следует исполнить.
    \note   Вся сгенерированная информация записывается в
            static массивы, указатели которых затем перемещаются.
*/
void generateCommandPack()
{ 
    isStringMode = 0;
    memset(&localTask, 0x00, sizeof(struct Task));
    pushParam();
    char ch = 0;
    #define CLEAR_STDIN { while (getchar() != '\n'); break; }
    while((ch = getchar()) != '\n' || isStringMode)
    {
        if(ch == EOF)
        {
            isEndOfFile = true;
            return;
        }
        if(ch == '#')
            CLEAR_STDIN;

        characherAnalysis(ch);
        if(localTask.isBackGround)
            CLEAR_STDIN;
    }
    #undef CLEAR_STDIN
    characherAnalysis(' ');
    pushCommand(); 
}


/**
    \brief   Функция генерит структуру Task по введенным данным
    \param   [in]  ptrToTask  указатель на Task* в который будет
                              записываться результат парса команды.
    \return  Возвращает true, если был встречен конец файла,
             false в противном случае.
*/
bool parseLine(struct Task** ptrToTask)
{
    Assert_addr(ptrToTask);
    generateCommandPack();
    *ptrToTask = calloc(1, sizeof(struct Task));
    Assert_addr(*ptrToTask);
    **ptrToTask = localTask;
    memset(&localTask, 0x00, sizeof(struct Task));
    return isEndOfFile;
}


/**
    \brief  Функция чистит структуру Command
    \param  [in]  cmd  Структура, данные в которой
                       следует освободить
*/
static void cleanUpCommand(struct Command cmd)
{
    free(cmd.strSizes);
    for(ui32 i = 0; i < cmd.argc; i++)
        free(cmd.argv[i]);
    free(cmd.argv);
}

/**
    \brief  Функция чистит структуру Task
    \param  [in]  task  Указатель на структура, данные в которой
                        следует освободить
    \note   Переденный указатель тоже очищается.
*/
void cleanUpTask(struct Task* task)
{
    if(!task) return;
    free(task->dividers);
    for(ui32 i = 0; i < task->nCommands; i++)
        cleanUpCommand(task->commands[i]);
    free(task->commands);
    free(task);
}
