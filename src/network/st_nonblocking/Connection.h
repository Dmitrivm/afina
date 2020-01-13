#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <array>
#include <cstring>
#include <sys/epoll.h>
#include <vector>

#include <spdlog/logger.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>

#include "protocol/Parser.h"

namespace Afina {
namespace Network {
namespace STnonblock {

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps, std::shared_ptr<spdlog::logger> pl)
        : _socket(s), pStorage(ps), _logger(pl) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
        offset = 0;
        length = 4096;
    }

    inline bool isAlive() const { return alive; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;
    // Connection States -------------------------------------------------------
    bool alive;
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::shared_ptr<Afina::Storage> pStorage;
    std::shared_ptr<spdlog::logger> _logger;
    std::unique_ptr<Execute::Command> command_to_execute;
    size_t length, offset;
    char client_buffer[4096];
    std::vector<std::string> result_buffer;
    size_t last_writed_bytes;
    //--------------------------------------------------------------------------
};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
