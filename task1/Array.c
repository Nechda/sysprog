#include "Array.h"
#include "StrLib.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MERGE_SORT 1
#define HEAP_SORT 2
#define SORT_ALORITHM HEAP_SORT

/**
\brief  Функция считывает числа из файла, генерируя массив
\param  [in]  filename  имя файла из которого будем читать 
\param  [in,out] retunredArraySize указатель на память куда
                                   будем записывать размер
                                   считанного массива
\retun  Указатель на сгенерированный массив
\note   В случае неудачной попытки будет возвращен NULL
*/
static int* readArrayFromFile(const char* filename, int* retunredArraySize)
{
    if(!retunredArraySize)
    {
        printf("Error: you should alloc memory for retunredArraySize variable\n");
        return NULL;
    }
    int* array = NULL;
    char* rawData = NULL;
    int size = readFullFile(filename, &rawData);
    if (size == STANDART_ERROR_CODE)
    {
        printf("Error: cant read file!\n");
        if(rawData)free(rawData);
        return NULL;
    }

    int arraySize = 0;
    bool wasSpace = 0;
    bool isSpace = 0;
    for(int i = 0; rawData[i]; i++)
    {
        isSpace = strchr(" \t\n",rawData[i]);
        arraySize += isSpace && !wasSpace;
        wasSpace = isSpace;
    };


    bool isLastDigit = !strchr(" \t\n",rawData[size-1]);
    arraySize += isLastDigit;
    if(!isLastDigit)
        rawData[size-1] = 0;

    array = (int*)calloc(arraySize,sizeof(int));
    if(!array)
    {
        printf("Error: Cant allocate memory for array of integers!\n");
        if(rawData)free(rawData);
        return NULL;
    }

    int offset = 0;
    for(int i = 0; i< arraySize;i++)
    {
        #define IS_DIGIT_CHARACTER\
            (rawData[offset] == '-' || ('0' <= rawData[offset] && rawData[offset] <= '9'))
        while( !IS_DIGIT_CHARACTER ) offset++;
        sscanf(&rawData[offset], "%d", &array[i]);
        while(  IS_DIGIT_CHARACTER ) offset++;
        #undef IS_DIGIT_CHARACTER
    }

    if(rawData)free(rawData);
    *retunredArraySize = arraySize;
    return array;
}


#if SORT_ALORITHM == MERGE_SORT

/// реализация сортировки слиянием
static void merge(int* array, int l, int m, int r)
{
    int n1 = m - l + 1;
    int n2 = r - m;
    int Left[n1];
    int Right[n2];
    for(int i = 0; i<n1;i++)
        Left[i] = array[l+i];
    for(int i=0;i<n2;i++)
        Right[i] = array[m+1+i];
    
    int i = 0;
    int j = 0;
    int k = l;
    while(i<n1 && j<n2)
    {
        if(Left[i] <= Right[j])
            array[k++] = Left[i++];
        else
            array[k++] = Right[j++];
    }

    while(i < n1)
        array[k++] = Left[i++];
    while(j < n2)
        array[k++] = Right[j++];
}

static void mergeSort(int* array, int l, int r)
{
    if(l>=r)
        return;
    int m = l + (r-l) / 2;
    mergeSort(array,l,m);
    mergeSort(array,m+1,r);
    merge(array,l,m,r);
}

#endif

#if SORT_ALORITHM == HEAP_SORT

///реализация сортировки кучей
static void heapify(int* array, int n, int i)
{
    int largest = i;
    int l = (i << 1) + 1;
    int r = l + 1; 

    if(l < n) largest = array[l] > array[largest] ? l : largest; 
    if(r < n) largest = array[r] > array[largest] ? r : largest; 

    if(largest != i)
    {
        array[i] ^= array[largest];
        array[largest] ^= array[i];
        array[i] ^= array[largest];

        heapify(array, n, largest);
    }
}

static void heapSort(int* array, int n)
{
    for(int i = (n>>1) - 1; i>= 0;i--)
        heapify(array, n, i);

    for(int i = n -1; i > 0; i--)
    {
        array[0] ^= array[i];
        array[i] ^= array[0];
        array[0] ^= array[i];

        heapify(array, i, 0);
    }
}

#endif


/**
    \brief  Функция сортирует массив целых чисел
    \param  [in]  array  указатель на массив
    \param  [in]  size   размер массива
*/
static void arraySorter(int* array, int size)
{
    if(!array)
    {
        printf("Error: invalid ptr to array\n");
        return;
    }
    if(size < 0)
    {
        printf("Error: array size must be non-negative number\n");
        return;
    }
    if(size == 1)
        return;
    
    #if SORT_ALORITHM == MERGE_SORT
        mergeSort(array, 0, size - 1);
    #else 
        heapSort(array, size);
    #endif
}

/**
    \brief  Функция печетает на экран массив целых чисел
    \param  [in]  array  указатель на массив
    \param  [in]  size   размер массива
*/
void arrayPrinter(int* array, int size)
{
    if(!array)
    {
        printf("Error: invalid ptr to array\n");
        return;
    }
    if(size < 0)
    {
        printf("Error: array size must be non-negative number\n");
        return;
    }
    for(int i = 0 ; i< size; i++)
        printf("%d ",array[i]);
}


/**
    \brief  Функция сортирует массив целых чисел, считанный из файла
    \param  [in]  filename  имя файла из которого считывается массив
    \return Возвращается структура типа Array
    \note   В случае возникновения ошибки поле data возвращаемой
            структуры будет равно NULL
*/
struct Array sortArrayFromFile(const char* filename)
{
    struct Array result;
    result.size = 0;
    result.data = NULL;
    
    if(!filename)
    {
        printf("Error: filename string contain null ptr.\n");
        return result;
    }

    result.data = readArrayFromFile(filename, &result.size);
    if(!result.data)
    {
        printf("Error: Cant read array from file\n");
        return result;
    }
    arraySorter(result.data, result.size);    
    return result;
}
