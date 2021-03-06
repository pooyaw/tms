#ifndef __IO_BUFFER_H__
#define __IO_BUFFER_H__

#include <sys/socket.h>

#include <assert.h>

#include <iostream>
#include <string>

#include "common_define.h"

const uint64_t kDefaultSize = 1024*64;
const uint64_t kShrinkSize = 2*1024*64;
const uint64_t kEnlargeSize = 1024*64;
const uint64_t kUdpMaxSize = 1460;

class IoBuffer
{
public:
    IoBuffer(const size_t& capacity = 0);
    virtual ~IoBuffer();

    virtual int ReadFromFdAndWrite(const int& fd);
    virtual int ReadFromFdAndWrite(const int& fd, sockaddr* addr, socklen_t* addr_len);
    virtual int WriteToFd(const int& fd);

    int Write(const std::string& data);
    int Write(const uint8_t* data, const size_t& len);
    int WriteU8(const uint8_t& u8);
    int WriteU16(const uint16_t& u16);
    int WriteU24(const uint32_t& u24);
    int WriteU32(const uint32_t& u32);
    int WriteU64(const uint64_t& u64);
    int WriteFake(const size_t& len);

    int ReadAndCopy(uint8_t* data, const size_t& len);
    int Read(uint8_t*& data, const size_t& len);
    int ReadU8(uint8_t& u8);
    int ReadU16(uint16_t& u16);
    int ReadU32(uint32_t& u32);
    int ReadU64(uint64_t& u64);

    int Peek(uint8_t*& data, const size_t& begin_pos, const size_t& len);
    int PeekU8(uint8_t& u8);
    int PeekU16(uint16_t& u16);
    int PeekU32(uint32_t& u32);
    int PeekU64(uint64_t& u64);

    int Skip(const size_t& len);

    bool Empty()
    {
        return Size() == 0;
    }

    size_t Size()
    {
        if (buf_ == NULL)
        {
            return 0;
        }

        if (end_ == start_)
        {
            ShrinkCapacity(kDefaultSize);

            //VERBOSE << LMSG << "adjust start_ and end_ to buf_" << std::endl;
        }

        assert(end_ >= start_);

        return end_ - start_;
    }

    int CapacityLeft()
    {
        return capacity_ - (end_ - buf_);
    }

protected:
    int MakeSpaceIfNeed(const size_t& len);
    void ShrinkCapacity(const size_t& capacity)
    {
        if (capacity_ > kShrinkSize)
        {
            std::cout << LMSG << "shrink " << capacity_ << "->" << capacity << std::endl;

            free(buf_);
            capacity_ = capacity;
            buf_ = (uint8_t*)malloc(capacity_);
        }

        start_ = buf_;
        end_ = buf_;
    }

protected:
    uint8_t *buf_;
    uint64_t capacity_;

    // buf [----(start)----------(end)----]
    //     |---->      capacity      <----|
    uint8_t *start_;
    uint8_t *end_;
};

#endif // __IO_BUFFER_H__
