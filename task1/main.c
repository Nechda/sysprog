#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>
#include <limits.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>

#include "Array.h"
#include "StrLib.h"

// время в микросекундах, через которое будет вызываться планировщик 
#define TIME_LEGACY 2000 

#define KB * 1024
#define MB * 1024 KB
#define STACK_SIZE 1 MB

#define handle_error_rude(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define say_error_and_return(msg) \
    do{printf("%s\n", msg); return;} while(0)


//==================================================================================================

//                               функции для работы с корутинами

//==================================================================================================


struct SchedulerInfo
{
    size_t swapTimes;
    size_t totalWakingTime;
};

static ucontext_t uctx_main;
static int nContexts = 0;
static int currentContextIndex = 0;

static ucontext_t* myContexts = NULL;
static struct Array* sortedArrays = NULL;
static struct SchedulerInfo* contextTimeInfo = NULL;
static clock_t currentClock = 0;


static ucontext_t signal_context;
static void *signal_stack; 

#define CLOCK_DELAY ( clock() - currentClock ); currentClock = clock()

/**
    \brief  Функция проверяет, все ли массивы отсортированны.
    \return true, в случае, когда все массивы отсортированы
            false иначе.
    \note   Если указатель sortedArrays содержит NULL, то
            возвращается true.
*/
static bool isAllArraySorted()
{
    if(!sortedArrays) return true;
    for(int i = 0; i < nContexts; i++)
        if(!sortedArrays[i].isSorted)
            return false;
    return true;
}

/**
    \brief  Функция выполняется на корутинах и выполняет сортировку
            указанного файла.
    \param  [in]  id        номер корутины
    \param  [in]  filename  строка, содержащая имя файла
    \note   После завершения сортирвки поле isSorted выставляется в
            true.
*/
static void doSorting(int id, const char* filename)
{
    sortedArrays[id] = sortArrayFromFile(filename);
    contextTimeInfo[id].totalWakingTime += CLOCK_DELAY;
    sortedArrays[id].isSorted = 1;
}

/**
    \brief  Функция выделяет память для стека корутин.
    \return Указатель на выделенную памать.
    \note   Размер выделяемой для стека памяти задан
            макросом STACK_SIZE.
*/
static void* allocate_stack_sig()
{
    void *stack = malloc(STACK_SIZE);
    stack_t ss;
    ss.ss_sp = stack;
    ss.ss_size = STACK_SIZE;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    return stack;
}


/**
    \brief  Функция создает корутину с заданным id и
            привязывает к ней файл.
    \param  [in]  id        номер корутины
    \param  [in]  filename  строка, содержащая имя файла 
*/
static void createCoroutine(int id, const char* filename)
{
    if (getcontext(&myContexts[id]) == -1)
        handle_error_rude("getcontext");
    myContexts[id].uc_stack.ss_sp = allocate_stack_sig();
    myContexts[id].uc_stack.ss_size = STACK_SIZE;
    myContexts[id].uc_stack.ss_flags = 0;
    myContexts[id].uc_link = &uctx_main;
    makecontext(&myContexts[id], doSorting, 2, id, filename);
}

/**
    \brief  Функция выделяет память, которая использутеся
            для работы планировщика и корутин.
    \param  [in]  nCount  число корутин
    \note   Стек планировщика имеет размер 4кБ
*/
static void allocateMemoryForCoroutine(int nCount)
{
    static bool ifFirtsTime = true;
    if(!ifFirtsTime) return;
    ifFirtsTime = false;
    myContexts = (ucontext_t*)calloc(nContexts,sizeof(ucontext_t));
    sortedArrays = (struct Array*)calloc(nContexts,sizeof(struct Array));
    contextTimeInfo = (struct SchedulerInfo*)calloc(nContexts, sizeof(struct SchedulerInfo));
    signal_stack = allocate_stack_sig();
}

/**
    \brief  Функция освобождает память, которая выделялась
            для планировщика и корутин.
    \param  [in]  nCount  число корутин
*/
static void cleanMemoryForCoroutine(int nCount)
{
    if(myContexts)
    for(int i = 0; i < nCount; i++)
    {
        if(myContexts[i].uc_stack.ss_sp) free(myContexts[i].uc_stack.ss_sp);
        myContexts[i].uc_stack.ss_sp = NULL;
    }
    if(myContexts) free(myContexts);
    
    if(sortedArrays)
    for(int i = 0; i < nCount; i++)
    {
        if(sortedArrays[i].data) free(sortedArrays[i].data);
        sortedArrays[i].data = NULL;
    }
    if(sortedArrays) free(sortedArrays);
    
    if(contextTimeInfo) free(contextTimeInfo);
    if(signal_stack) free(signal_stack);
    myContexts = NULL;
    sortedArrays = NULL;
    contextTimeInfo = NULL;
    signal_stack = NULL;
}

//==================================================================================================
//==================================================================================================






//==================================================================================================

//                               планировщик и установка таймера

//==================================================================================================


sigset_t set; 
struct itimerval timer;

/**
    \brief    Простейший планировщих корутин.
    \details  Планировщик вызывается по прерыванию от таймера
              каждые TIME_LEGACY микросекнуд, при этом выбирается
              та корутина, которая все еще сортирует свой массив.
*/
void scheduler()
{
    int oldIndex = currentContextIndex;
    do
    {
        currentContextIndex++;
        currentContextIndex %= nContexts;
    }while(sortedArrays[currentContextIndex].isSorted);

    if(currentContextIndex!=oldIndex)
        contextTimeInfo[oldIndex].swapTimes++;
    
    setcontext(&myContexts[currentContextIndex]);
}


static void timer_off()
{
    timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        timer.it_value = timer.it_interval;
        if (setitimer(ITIMER_REAL, &timer, NULL) ) perror("setitiimer");
}

/**
    \brief  Обработчик таймера, вызывающий планировщик.
    \note   Если все массивы отсортированы, то таймер выключается.
*/
void timer_interrupt(int j, siginfo_t *si, void *old_context)
{
    if(isAllArraySorted())
    {
        timer_off();
        return;
    }

    getcontext(&signal_context);
    signal_context.uc_stack.ss_sp = signal_stack;
    signal_context.uc_stack.ss_size = STACK_SIZE;
    signal_context.uc_stack.ss_flags = 0;
    sigemptyset(&signal_context.uc_sigmask);
    makecontext(&signal_context, scheduler, 1);

    contextTimeInfo[currentContextIndex].totalWakingTime+=CLOCK_DELAY;
    swapcontext(&myContexts[currentContextIndex],&signal_context);
}

/**
    \brief  Функция устанавливает обработчик для таймера 
*/
static void setup_signals(void)
{
    struct sigaction act;

    act.sa_sigaction = timer_interrupt;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART | SA_SIGINFO;

    sigemptyset(&set);
    sigaddset(&set, SIGALRM);

    if(sigaction(SIGALRM, &act, NULL) != 0)
        handle_error_rude("Signal handler");
}


//==================================================================================================
//==================================================================================================


/**
    \brief  Функция объединяет все отсортированные массивы
            в один большой отсортированный массив, а
            результат записывается в файл.
    \param  [in]  filename  имя файла, в который будет
                            производиться запись
*/
static void writeArraysInFile(const char* filename)
{
    if(!filename)
        say_error_and_return("filename ptr contain null ptr.");
    FILE* outFile = fopen(filename, "w");
    if(!outFile)
        say_error_and_return("Cant open file for writing.");

    
    int index[nContexts];
    for(int i = 0; i < nContexts; i++)
        index[i] = 0;

    short nArraysWritedAlready = 0;
    for(int i = 0; i < nContexts; i++)
        if(sortedArrays[i].size == 0)
        {
            sortedArrays[i].isSorted = 0;
            nArraysWritedAlready++;
        }
        
        
    while(nArraysWritedAlready != nContexts)
    {
        int min = INT_MAX;
        int tmp = 0;
        //поиск минимального элемента
        for(int i = 0; i < nContexts; i++)
        {
            if(!sortedArrays[i].isSorted)
                continue;
            if(index[i] >= sortedArrays[i].size)
            {
                sortedArrays[i].isSorted = 0;
                nArraysWritedAlready++;
            }
            tmp = sortedArrays[i].data[index[i]];
            if(min > tmp)
                min = tmp;
        }
        
        if(nArraysWritedAlready == nContexts)
            break;
        
        
        //затем продвижение указателей в массивах до тех пор, пока не встретим новое число
        tmp = 0; //будет хранить количество чисел во всех массивах, которые равны min
        for(int i = 0; i < nContexts; i++)
            if(sortedArrays[i].isSorted)
            {
                if(min != sortedArrays[i].data[index[i]])
                    continue;
                while(min == sortedArrays[i].data[index[i]])
                {
                    index[i]++;
                    tmp++;
                    if(index[i] == sortedArrays[i].size)
                    {
                        sortedArrays[i].isSorted = 0;
                        nArraysWritedAlready++;
                        break;
                    }
                }
            }
        
        //ну и печатаем в файл
        while(tmp--)
            fprintf(outFile, "%d ", min);
    }

    fclose(outFile);
}

int main(int argc, char *argv[])
{
    //чекаем количество переденных файлов
    if(argc == 1)
    {
        printf("You should select at least one file for sorting.\n");
        return 0;
    }


    nContexts = argc - 1;
    //проверяем, что все файлы, которые нам указали, доступны
    for(int i = 0; i < nContexts; i++)
        if(access( argv[i+1], F_OK ))
        {
            printf("Error: file `%s` does not exist!\n",argv[i+1]);
            return 0;
        }

    //выделяем память
    allocateMemoryForCoroutine(nContexts);
    for(int i = 0; i < nContexts; i++)
        createCoroutine(i, argv[i+1]);

    //устанавливаем таймер
    setup_signals();
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = TIME_LEGACY;
    timer.it_value = timer.it_interval;
    currentClock = clock();
    if (setitimer(ITIMER_REAL, &timer, NULL) ) perror("setitiimer");
    
    //запускаем сортировку
    swapcontext(&uctx_main, &myContexts[0]);

    //ждем, пока все закончат сортировать
    while(!isAllArraySorted()){;;}
    timer_off();
    

    //выводим инфу о том, сколько работали корутины
    for(int i = 0; i<nContexts; i++)
    {
        printf("cour[%d]: swap_times: %04ld, total working time %05ld us\n",
            i, contextTimeInfo[i].swapTimes,
            contextTimeInfo[i].totalWakingTime
        );
    }

    printf("Finally files have been sorted, but now we will start create another file!\n");

    clock_t start = clock();
    //производим конкатенацию всех файлов
    writeArraysInFile("sorted.txt");
    clock_t end = clock();
    clock_t uSeconds = end-start;
    double seconds = (double)uSeconds/CLOCKS_PER_SEC;
    printf("Writing to the file took %04ld us (%04lf s)\n", uSeconds, seconds);
    
    //и чистим память
    cleanMemoryForCoroutine(nContexts);
    return 0;
}
