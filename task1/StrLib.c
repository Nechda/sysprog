#include "StrLib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <aio.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

/**
    \brief  Функция полностью сичтывает файл
    \param  [in]      filename  Имя считываемого файла
    \param  [in,out]  outString Указатель на считанную строку
    \return В случае успеха возвращается количество прочитанных байт.
    Если произошла ошибка, то возвращается константа -1.
*/
int readFullFile(const char* filename, char** outString)
{
    assert(filename);
    assert(outString);
    if (!filename || !outString)
        return STANDART_ERROR_CODE;

    FILE* inputFile = fopen(filename, "rb");
    assert(inputFile);
    if (!inputFile)
        return STANDART_ERROR_CODE;
    if (ferror(inputFile))
        return STANDART_ERROR_CODE;

    fseek(inputFile, 0, SEEK_END);
    long fsize = ftell(inputFile);
    fseek(inputFile, 0, SEEK_SET);

    char* string = (char*)calloc(fsize + 8, sizeof(char));
    assert(string);
    if (!string)
        return STANDART_ERROR_CODE;

    unsigned nReadBytes = fread(string, sizeof(char), fsize, inputFile);
    fclose(inputFile);
    string[fsize] = 0;

    *outString = string;

    return nReadBytes;
}




/**
    \brief  Функция полностью сичтывает файл используя aio_read()
    \param  [in]      filename  Имя считываемого файла
    \param  [in,out]  outString Указатель на считанную строку
    \return В случае успеха возвращается количество прочитанных байт.
    Если произошла ошибка, то возвращается константа -1.
*/
int async_readFullFile(const char* filename, char** outString)
{
    assert(filename);
    assert(outString);
    if (!filename || !outString)
        return STANDART_ERROR_CODE;

    int fd =  open(filename, O_RDWR | O_EXCL, S_IRUSR | S_IWUSR);
    assert(fd != -1);
    if(fd == -1)
    {
        printf("Failed open file for reading.\n");
        return STANDART_ERROR_CODE;
    }

    long fsize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    char* string = (char*)calloc(fsize + 8, sizeof(char));
    assert(string);
    if (!string)
        return STANDART_ERROR_CODE;


    struct aiocb aiocb;
    memset(&aiocb, 0, sizeof(struct aiocb));
    aiocb.aio_fildes = fd;
    aiocb.aio_buf = string;
    aiocb.aio_nbytes = fsize;

    if(aio_read(&aiocb) == -1)
    {
        printf("Error at aio_read()\n");
        close(fd);
        free(string);
        return STANDART_ERROR_CODE;
    }

    int err,ret;
    while ((err = aio_error (&aiocb)) == EINPROGRESS);
    err = aio_error(&aiocb);
    ret = aio_return(&aiocb);


    unsigned nReadBytes = ret;
    close(fd);
    string[fsize] = 0;

    *outString = string;

    return nReadBytes;
}
