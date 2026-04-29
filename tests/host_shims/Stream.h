#ifndef TESTS_HOST_SHIMS_STREAM_H
#define TESTS_HOST_SHIMS_STREAM_H

#include <stddef.h>

class Stream
{
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual size_t readBytes(char *buffer, size_t length) = 0;
    virtual void setTimeout(unsigned long timeout)
    {
        (void)timeout;
    }
};

#endif // TESTS_HOST_SHIMS_STREAM_H