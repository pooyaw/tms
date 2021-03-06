#ifndef __HTTP_FILE_PROTOCOL_H__
#define __HTTP_FILE_PROTOCOL_H__

#include <stdint.h>

#include <string>

#include "http_parse.h"
#include "socket_handler.h"

class IoLoop;
class Fd;
class IoBuffer;
class TcpSocket;

class HttpFileProtocol
    : public SocketHandler
{
public:
    HttpFileProtocol(IoLoop* io_loop, Fd* socket);
    ~HttpFileProtocol();

	virtual int HandleRead(IoBuffer& io_buffer, Fd& socket);
    virtual int HandleClose(IoBuffer& io_buffer, Fd& socket) 
    { 
        UNUSED(io_buffer);
        UNUSED(socket);

        return kSuccess;
    }

    virtual int HandleError(IoBuffer& io_buffer, Fd& socket) 
    { 
        return HandleClose(io_buffer, socket); 
    }

    int Parse(IoBuffer& io_buffer);
    int Send(const uint8_t* data, const size_t& len);

    int EveryNSecond(const uint64_t& now_in_ms, const uint32_t& interval, const uint64_t& count);

    int EveryNMillSecond(const uint64_t& now_in_ms, const uint32_t& interval, const uint64_t& count) { return 0; }


private:
    TcpSocket* GetTcpSocket()
    {   
        return (TcpSocket*)socket_;
    }

private:
    IoLoop* io_loop_;
    Fd* socket_;
    HttpParse http_parse_;

    bool upgrade_;
};

#endif // __HTTP_FILE_PROTOCOL_H__
