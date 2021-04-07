#include <stdlib.h>
#include <stdio.h>
#include "AssertAddr.h"
#include "Ringbuffer.h"

/*
    Для хранения информации о процессах, которые исполняются
    в фоновом режиме будем спользовать кольцевой буфер.
    Каждая ячейка такого буфера будет содердать информацию
    о pid процесса который выполняется в фоне и указатель
    на структуру Task, которая содержит в себе информацию
    о том, какой именно командой был вызван данный процесс.

    Для простоты реализации в качетсве начала кольцевого
    буфера будем использовать статическую переменную root.

    Ниже представлены функции, обеспечивающие работу с
    кольцевым буфером.
*/


///< Структура для реализации ячейки в кольцевом буфере
struct Node
{
    pid_t pid;
    struct Task* task;
    struct Node* next;
};

static struct Node root = {0, 0, 0}; 

///< Инициализирует начальный элемент в корневом буфере
void ringBufferInit()
{
    root.next = &root;
}

/**
    \brief  Функция записывает новый элемент в буфер.
    \param  [in]  pid   Pid процесса, который будет
                        работать в фоновом режиме.
    \param  [in]  task  Указатель на структуру Task
                        с информацией о том, как этот
                        процесс был вызван.
*/
void ringBufferPushItem(pid_t pid, struct Task* task)
{
    if(!task)
        return;
    struct Node* newNode = calloc(1, sizeof(struct Node));
    Assert_addr(newNode);
    newNode->pid = pid;
    newNode->task = task;
    newNode->next = root.next;
    root.next = newNode;
}

/**
    \brief  Функция удаляет элемент из буфера с заданным pid.
    \param  [in]  pid  Pid процесса, который нужно удалить из
                       буфера.
*/
void ringBufferDelItem(pid_t pid)
{
    struct Node* cur = root.next;
    struct Node* prev = &root;
    do
    {
        prev = cur;
        cur = cur->next;
    }while(cur != &root && cur->pid != pid);

    if(cur == &root)
        return;

    cleanUpTask(cur->task);
    prev->next = cur->next;
    free(cur);
}


/**
    \brief   Функция очищает все содержимое буфера 
*/
void ringBufferCleanUp()
{
    struct Node* cur = root.next;
    if(cur != &root)
    do
    {
        cleanUpTask(cur->task);
        root.next = cur->next;
        free(cur);
        cur = root.next;
    } while (root.next != &root);
}
