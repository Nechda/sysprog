#include "userfs.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>


#define RETURN_ERROR(error_code) do{ ufs_error_code = error_code; return -1; } while(0)


enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 1024,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block
{
	char *memory;
	int occupied;
	struct block *next;
	struct block *prev;
};


/*
    \biref  Функция добавляет новый блок к передаваемому
    \param  [in]  last  Указатель на последний блок, к которому
                        будет присоединен новый блок.
    \return Указатель на новый блок.
*/
static struct block*
push_back_new_block(struct block* last)
{
    struct block* result = calloc(1,sizeof(struct block));
    result->memory = calloc(BLOCK_SIZE, sizeof(char));

    last->next = result;
    result->prev = last;
    return result;
}

/*
    \biref  Функция удаляет последний блок в списке.
    \param  [in]  last  Указатель на последний блок, который удаляется.
    \return Указатель на предыдущий блок в списке, если такого нет, то
            возвращается NULL.
*/
static struct block*
pop_back_block(struct block* last)
{
    struct block* result = last->prev;
    result->next = NULL;
    free(last->memory);
    free(last);
    return result;
}


struct file
{
	struct block *block_list;
	struct block *last_block;
    size_t size;
    bool isGhost;
	int refs;
	const char *name;
	struct file *next;
	struct file *prev;
};

/** List of all files. */
static struct file *file_list = NULL;


void debug_print_files()
{
    struct file* curr = file_list;
    while(curr)
    {
        printf("File{\n  name:%s\n  isGhost: %d\n}\n", curr->name,curr->isGhost);
        curr=curr->next;
    }
}



/*
    \brief  Функция выполняет поиск файла в файловой системе.
    \param  [in]  filename  Имя файла, который требуется найти.
    \return Если файл найден в системе, то возвращается указатель
            на cтруктуру file. В противном случае возвращается NULL.
*/
static struct file*
find_file(const char* filename)
{
    struct file* curr = file_list;
    if(!curr)
        return NULL;

    if(!strcmp(curr->name, filename))
        return curr;
    do
    {
        curr = curr->next;
        if(!curr) break;
    }while(curr != file_list && strcmp(curr->name, filename));
    return curr == file_list ? NULL : curr;
}

/*
    \brief  Функция создает новый файл в файловой системе.
    \param  [in]  filename  Имя нового файла.
    \return Указатель на структуру file созданного файла.
            В случае ошибки возвращается NULL.
*/
struct file*
create_file(const char* filename)
{
    if(!filename)
    {
        ufs_error_code = UFS_ERR_NULL_PTR_BUF;
        return NULL;
    }

    struct file* result = calloc(1, sizeof(struct file));
    int filename_len = strlen(filename) + 1;
    result->name = calloc(filename_len, sizeof(char));
    memcpy((void*)result->name, filename, filename_len);


    if(file_list)
    {
        //теперь добавляем в общий набор файлов
        result->prev = NULL;
        result->next = file_list;
        file_list->prev = result;
    }
    file_list = result;

    return result;
}

/** Union for simply access to rights **/
union Rights
{
    int8_t bits;
    struct
    {
        int8_t isCreated  : 1;
        int8_t isReadable : 1;
        int8_t isWritable : 1;
        int8_t isAppend   : 1;
        int8_t reserved   : 4;
    };
};

struct filedesc
{
	struct file *file;
    struct block* current_block;
    size_t pos_reading;
    size_t pos_writing;
    union Rights rights;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc** file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;





void debug_print_descriptors()
{
    for(int i = 0; i < file_descriptor_capacity; i++)
    {
        if(file_descriptors[i])
            printf("Descriptor[%d]{\n    file:%p\n     write_pos:%lu\n    read__pos:%lu\n}\n",i,
            file_descriptors[i]->file, file_descriptors[i]->pos_writing, file_descriptors[i]->pos_reading);
        else
            printf("Descriptor[%d]{\n}\n",i);
    }
}

/*
    \brief  Функция ищет свободное место в массиве дескрипторов.
    \return Индекс в массиве дескрипторов, где имеется место для
            записи. В случае отсутствия места в массиве 
            возвращается -1.
*/
static int
get_free_space_in_fd_array()
{
    if(!file_descriptors
    ||  file_descriptor_count >= file_descriptor_capacity
    )
        return -1;

    for(int i = 0; i < file_descriptor_capacity; i++)
        if(!file_descriptors[i])
        return i;
    
    return -1;
}



/*
    \brief  Функция увеличивает размер массива дескрипторов на единицу
    \return Указатель на новый массив дескрипторов.
*/
static struct filedesc**
resize_fd_array()
{
    file_descriptor_capacity++;
    file_descriptors = realloc(file_descriptors, sizeof(struct filedesc*) * file_descriptor_capacity);
    memset(&file_descriptors[file_descriptor_capacity-1], 0x00, sizeof(struct filedesc*));
    return &file_descriptors[file_descriptor_capacity-1];
}


enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int
ufs_open(const char *filename, int flags)
{
    //подсунули нулевой указатель
    if(!filename)
        return -1;

    struct file* opened_file = find_file(filename);


    union Rights rights;
    rights.bits = flags;
    if(!rights.isReadable && !rights.isWritable)
        rights.bits |= UFS_READ_WRITE | USF_APPEND;

    bool isNeedCreate = rights.isCreated;

    bool isExit = opened_file;
    bool isGhost = isExit ? opened_file->isGhost : false;

    //не существует файла
    if(!isExit && !isNeedCreate)
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    //пытаемся обратиться к несуществующему 'ghost' файлу
    if(isExit && isGhost && !isNeedCreate)
    {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    //создаем файл
    if(!isExit && isNeedCreate)
        opened_file = create_file(filename);


    if(isExit && isGhost && isNeedCreate)
        opened_file = create_file(filename);


    int fd = get_free_space_in_fd_array();
    if(fd == -1)
    {
        fd = file_descriptor_count;
        resize_fd_array();
    }

    file_descriptors[fd] = calloc(1,sizeof(struct file));

    file_descriptors[fd]->file = opened_file;
    file_descriptors[fd]->rights = rights;
    file_descriptor_count++;
    opened_file->refs++;

	return fd;
}



ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
    if(fd < 0 || fd >= file_descriptor_capacity)
        RETURN_ERROR(UFS_ERR_NO_FILE);

    if(!buf)
        RETURN_ERROR(UFS_ERR_NULL_PTR_BUF);

    if(!file_descriptors[fd]->rights.isWritable)
        RETURN_ERROR(UFS_ERR_NO_PERMISSION);

    struct file* f = file_descriptors[fd]->file;
    if(!f)
        RETURN_ERROR(UFS_ERR_NO_FILE);

    struct block* blk = f->block_list;
    //создание блока, если у файла нет ни одного
    if(!blk)
    {
        f->block_list = calloc(1, sizeof(struct block));
        blk = f->block_list;
        f->last_block = blk;

        blk->memory = calloc(BLOCK_SIZE, sizeof(char));
        blk->occupied = 0;
    }

    struct filedesc* s_fd = file_descriptors[fd];

    if(s_fd->pos_writing > f->size)
    {
        s_fd->pos_writing = f->size;
        s_fd->current_block = NULL;
    }

    // если доступно чтение и запись, то не используем `current_block`
    if(!(s_fd->rights.isWritable ^ s_fd->rights.isReadable))
        s_fd->current_block = NULL;

    //переходим на блок, на который указывает позиция
    if(s_fd->rights.isAppend)
    {
        s_fd->pos_writing = s_fd->file->size;
        if(s_fd->pos_writing >= MAX_FILE_SIZE)
            RETURN_ERROR(UFS_ERR_NO_MEM);

        blk = f->last_block;
        if(blk->occupied == BLOCK_SIZE)
        {
            blk = push_back_new_block(blk);
            f->last_block = blk;
        }
    }
    else
    {

        int index = s_fd->pos_writing;
        if(index >= MAX_FILE_SIZE)
            RETURN_ERROR(UFS_ERR_NO_MEM);

        if(s_fd->current_block)
            blk = s_fd->current_block;
        else
        {
            index = index / BLOCK_SIZE;
            struct block* prev_blk = NULL;
            while(index--)
            {
                prev_blk = blk;
                blk = blk->next;
            }

            if(!blk)
            {
                blk = push_back_new_block(prev_blk);
                f->last_block = blk;
            }
        }

    }
    s_fd->current_block = blk;


    int pos_in_blk = s_fd->pos_writing % BLOCK_SIZE;
    int original_size = size;
    //теперь в blk лежит тот блок, в который нужно писать
    while(size)
    {
        if(pos_in_blk == BLOCK_SIZE)
        {
            if(!blk->next)
            {
                blk = push_back_new_block(blk);
                f->last_block = blk;
            }
            else
                blk = blk->next;
            pos_in_blk = 0;
        }
        

        int n_written_bytes = BLOCK_SIZE - pos_in_blk;
        n_written_bytes =  size > n_written_bytes ? n_written_bytes : size;
        memcpy(&blk->memory[pos_in_blk], buf, n_written_bytes);
        s_fd->pos_writing += n_written_bytes;
        pos_in_blk += n_written_bytes;
        blk->occupied = blk->occupied > pos_in_blk ? blk->occupied : pos_in_blk;
        buf += n_written_bytes;
        size -= n_written_bytes;
    }
    size_t old_size = s_fd->file->size;
    size_t new_size = s_fd->pos_writing;
    if(new_size > old_size)
    {
        s_fd->file->size = new_size;
        s_fd->file->last_block = blk;
    }
    
    return original_size - size;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
    if(fd < 0 || fd >= file_descriptor_capacity)
        RETURN_ERROR(UFS_ERR_NO_FILE);

    if(!buf)
        RETURN_ERROR(UFS_ERR_NULL_PTR_BUF);

    if(!file_descriptors[fd]->rights.isReadable)
        RETURN_ERROR(UFS_ERR_NO_PERMISSION);

    struct file* f = file_descriptors[fd]->file;
    if(!f)
        RETURN_ERROR(UFS_ERR_NO_FILE);

    struct block* blk = f->block_list;
    // если файл пуст, то считать можно только 0 байт
    if(!blk)
        return 0;

    struct filedesc* s_fd = file_descriptors[fd];

    // если доступно чтение и запись, то не используем `current_block`
    if(!(s_fd->rights.isWritable ^ s_fd->rights.isReadable))
        s_fd->current_block = NULL;

    if(s_fd->pos_reading > f->size)
    {
        s_fd->pos_reading = f->size;
        s_fd->current_block = NULL;
    }

    //переходим на блок, на который указывает позиция
    if(s_fd->current_block)
        blk = s_fd->current_block;
    else
    {
        int index = s_fd->pos_reading / BLOCK_SIZE;
        while(index--)
            blk = blk->next;
        s_fd->current_block = blk;
    }

    int pos_in_blk = s_fd->pos_reading % BLOCK_SIZE;

    bool isEOF = !blk;
    int original_size = size;
    //теперь в blk лежит тот блок, из которого требуется читать информацию
    while(size && !isEOF)
    {
        isEOF = pos_in_blk == blk->occupied && !blk->next;
        if(isEOF) break;
        if(pos_in_blk == BLOCK_SIZE)
        {
            blk = blk->next;
            pos_in_blk = 0;
        }

        int n_read_bytes = (blk->occupied - pos_in_blk);
        n_read_bytes =  size > n_read_bytes ? n_read_bytes : size;
        memcpy(buf, &blk->memory[pos_in_blk], n_read_bytes);
        s_fd->pos_reading += n_read_bytes;
        pos_in_blk += n_read_bytes;
        buf += n_read_bytes;
        size -= n_read_bytes;
    }

    return original_size - size;
}


/*
    \brief  Функция выполняет чистку структуры file.
    \param  [in]  f  Указатель на структуру файла.
    \note   После выполнения функции передаваемый указатель
            НЕ нужно очищать.
*/
static void
file_clean_up(struct file* f)
{
    struct block* cur = f->block_list;
    struct block* next = NULL;
    while(cur)
    {
        free(cur->memory);
        next = cur->next;
        free(cur);
        cur = next;
    }
    free((void*)f->name);
    free(f);
}

/*
    \brief  Функция удаляет файл из списка файлов.
    \param  [in]  f  Указатель на фалй, который
                     требуется удалить из списка.
*/
static void
remove_file_from_list(struct file* f)
{
    if(file_list == f)
    {
        file_list = f->next;
        file_clean_up(f);
    }
    else
    {
        if(f->prev) f->prev->next = f->next;
        if(f->next) f->next->prev = f->prev;
        file_clean_up(f);
    }
}

int
ufs_close(int fd)
{
	if(fd >= file_descriptor_capacity || fd < 0)
        RETURN_ERROR(UFS_ERR_NO_FILE);
    
    if(!file_descriptors[fd])
        RETURN_ERROR(UFS_ERR_NO_FILE);


    file_descriptors[fd]->file->refs--;
    // чистим массив дескрипторов, если файл уже удален и
    // и была последняя ссылка
    {
        struct file* f = file_descriptors[fd]->file;
        if(f->isGhost && !f->refs)
            remove_file_from_list(f);
    }
    free(file_descriptors[fd]);
    file_descriptors[fd] = NULL;
    file_descriptor_count--;


    int total_free_space = 0;
    for(int i = 0; i < file_descriptor_capacity; i++)
        total_free_space += !file_descriptors[i];
    if(total_free_space == file_descriptor_capacity)
    {
        free(file_descriptors);
        file_descriptor_capacity = 0;
        file_descriptor_count = 0;
        file_descriptors = NULL;
    }
	return 0;
}



int
ufs_delete(const char *filename)
{
    struct file* f = find_file(filename);
    //такого файла не существует
    if(!f)
    RETURN_ERROR(UFS_ERR_NO_FILE);
    if(f->isGhost)
        return 0;
    f->isGhost = f->refs;
    if(f->refs == 0)
        remove_file_from_list(f);
    return 0;
}



int
ufs_resize(int fd, size_t new_size)
{
    if(fd < 0 || fd > file_descriptor_capacity)
        RETURN_ERROR(UFS_ERR_NO_FILE);

    struct file* f = file_descriptors[fd]->file;
    if(!f)
        RETURN_ERROR(UFS_ERR_NO_FILE);

    if(new_size > MAX_FILE_SIZE)
        RETURN_ERROR(UFS_ERR_NO_MEM);
    
    struct block* blk = f->last_block;
    int need_bytes = 0;

    if(new_size == 0)
    {
        while(blk)
            blk = pop_back_block(blk);
        f->block_list = NULL;
        f->last_block = NULL;
    }

    if(new_size > f->size)
    {
        need_bytes = new_size - f->size;
        while(need_bytes > BLOCK_SIZE)
        {
            need_bytes -= BLOCK_SIZE - blk->occupied;
            blk = push_back_new_block(blk);
        }
        if(need_bytes)
        {
            blk = push_back_new_block(blk);
            blk->occupied = need_bytes;
        }
        f->last_block = blk;
    }

    if(new_size < f->size && new_size > 0)
    {
        need_bytes = f->size - new_size;

        while(need_bytes > BLOCK_SIZE && blk)
        {
            need_bytes -= blk->occupied;
            blk = pop_back_block(blk);
            if(!blk)
                f->block_list = NULL;
        }
        if(need_bytes && blk)
            blk->occupied -= need_bytes;
        f->last_block = blk;
    }
    
    file_descriptors[fd]->current_block = NULL;
    f->size = new_size;
    
    return 0;
}
