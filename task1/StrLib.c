#include "StrLib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>


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