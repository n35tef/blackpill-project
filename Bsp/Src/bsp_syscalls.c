/**
 * @file    bsp_syscalls.c
 * @brief   Minimal newlib syscall stubs for a bare-metal target.
 *
 * newlib expects a handful of POSIX-ish primitives to exist even in programs
 * that never use them; without these the link fails with "undefined reference
 * to _sbrk" and friends. Only _sbrk does real work (it feeds malloc from the
 * region the linker script reserved between .bss and the stack).
 *
 * Retarget stdout by defining bsp_putchar() somewhere in your project - for
 * example forwarding to a USART - and printf() will start working.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Linker script symbols delimiting the heap. */
extern uint8_t _end;
extern uint8_t _estack;
extern uint32_t _Min_Stack_Size;

#undef errno
extern int errno;

/**
 * @brief Single-character output hook used by _write().
 *
 * Weak and discarding by default. Override it to route printf() somewhere.
 */
__attribute__((weak)) int bsp_putchar(int ch)
{
    (void)ch;
    return -1;
}

/** @brief Single-character input hook used by _read(). */
__attribute__((weak)) int bsp_getchar(void)
{
    return -1;
}

/** @brief Grow the heap, stopping short of the reserved stack region. */
void* _sbrk(ptrdiff_t incr)
{
    static uint8_t* heap_end = NULL;

    if (heap_end == NULL)
    {
        heap_end = &_end;
    }

    uint8_t* const heap_limit = &_estack - (uintptr_t)&_Min_Stack_Size;

    if ((heap_end + incr) > heap_limit)
    {
        errno = ENOMEM;
        return (void*)-1;
    }

    uint8_t* const previous = heap_end;
    heap_end += incr;
    return previous;
}

int _write(int file, const char* ptr, int len)
{
    (void)file;

    for (int i = 0; i < len; i++)
    {
        if (bsp_putchar((int)(unsigned char)ptr[i]) < 0)
        {
            return i;
        }
    }
    return len;
}

int _read(int file, char* ptr, int len)
{
    (void)file;

    for (int i = 0; i < len; i++)
    {
        const int ch = bsp_getchar();
        if (ch < 0)
        {
            return i;
        }
        ptr[i] = (char)ch;
    }
    return len;
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat* st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    while (1)
    {
    }
}
