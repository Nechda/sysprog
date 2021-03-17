#pragma once


#define STANDART_ERROR_CODE -1

int readFullFile(const char* filename, char** outString);
int async_readFullFile(const char* filename, char** outString);