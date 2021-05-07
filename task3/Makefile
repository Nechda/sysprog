CC = clang
CFLAGS = -fsanitize=address -Werror -Wall

a.out: test.c userfs.c
    $(CC) $(CFLAGS) -g test.c userfs.c

.PHONY: run
run: a.out
    ./a.out

.PHONY: clean
clean:
    rm -f a.out
