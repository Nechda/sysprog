#ifndef ASSERT_ADDR_H
#define ASSERT_ADDR_H

#define Assert_addr(addr)                                    \
    if(!addr)                                                \
    {                                                        \
        perror("Allocated memory contains NULL.");           \
        exit(EXIT_FAILURE);                                  \
    }

#endif
