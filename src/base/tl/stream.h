#ifndef BASE_TL_STREAM_H
#define BASE_TL_STREAM_H

#include <base/system.h>
#include "array.h"

class stream
{
public:
    stream() {};
    virtual ~stream() {};

    virtual unsigned write(const unsigned char *buffer, unsigned size) = 0;
    virtual unsigned write_newline() = 0;
};

class file_stream : public stream
{
    IOHANDLE file;
    bool auto_io_close;
public:
    file_stream(IOHANDLE handle, bool auto_close)
    {
        file = handle;
        auto_io_close = auto_close;
    }

    virtual ~file_stream()
    {
        io_close(file);
    }
    
    virtual unsigned write(const unsigned char *buffer, unsigned size)
    {
        return io_write(file, buffer, size);
    }

    virtual unsigned write_newline()
    {
        return io_write_newline(file);
    }
};

class inplace_memory_stream : public stream
{
    unsigned char *start;
    unsigned char *end;
public:
    inplace_memory_stream(unsigned char *memory, unsigned size)
    {
        start = memory;
        end = start + size;
    }

    virtual unsigned write(const unsigned char *buffer, unsigned size)
    {
        if(start + size > end)
            return 0;
        mem_copy(start, buffer, size);
        return size;
    }

    virtual unsigned write_newline()
    {
        if(start >= end)
            return 0;
        *start = '\n';
        start++;
        return 1;
    }
};

template<class T>
class memory_stream : public stream
{
    static_assert(sizeof(T) == 1, "the size of buffer element should be 1 byte");
    array<T> *mem_buffer;
public:
    memory_stream(array<T> *buffer)
    {
        mem_buffer = buffer;
        buffer->clear();
    }

    virtual unsigned write(const unsigned char *buffer, unsigned size)
    {
        unsigned start = mem_buffer->size();
        mem_buffer->hint_size(start + size);
        inplace_memory_stream stream((unsigned char *) mem_buffer->base_ptr() + start, size);
        return stream.write(buffer, size);
    }

    virtual unsigned write_newline()
    {
        return write((const unsigned char *)("\n"), 1);
    }
};


#endif // BASE_TL_STREAM_H