/*
 * printf.h
 */

#ifndef CORE_INC_PRINTF_H_
#define CORE_INC_PRINTF_H_

//#define NO_PRINT

#ifdef NO_PRINT
    #define PRINTF(...) ((void)0)
    #define printf(...) ((void)0)
#else
    #define PRINTF(...) printf(__VA_ARGS__)
#endif

#endif /* CORE_INC_PRINTF_H_ */
