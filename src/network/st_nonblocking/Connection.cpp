#include "Connection.h"

#include <iostream>
#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start() {
    _event.events |= EPOLLIN | EPOLLRDHUP | EPOLLERR;
    alive = true;
}

// See Connection.h
void Connection::OnError() { alive = false; }

// See Connection.h
void Connection::OnClose() {
    close(_socket);
    alive = false;
}

// See Connection.h
void Connection::DoRead() {

    try {
        int readed_bytes = -1;
        while ((readed_bytes = read(_socket, client_buffer + offset, length)) > 0) {
            _logger->debug("Got {} bytes from socket", readed_bytes);
            length -= readed_bytes;
            offset += readed_bytes;
            // Single block of data readed from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            while (offset > 0) {
                _logger->debug("Process {} bytes", readed_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer, offset, parsed)) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0) {
                            arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, offset - parsed);
                        offset -= parsed;
                        length += parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(offset));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, offset - to_read);
                    arg_remains -= to_read;
                    offset -= to_read;
                    length += to_read;
                }

                // There is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");

                    std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    // Send response
                    result += "\r\n";
                    result_buffer.push_back(result);
                    _event.events |= EPOLLOUT;
                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }

        if (readed_bytes == 0) {
            _logger->debug("Connection closed");
        } else {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }
}

// See Connection.h
void Connection::DoWrite() {

    size_t output_size = result_buffer.size();
    if (result_buffer.size() > 64) {
        output_size = 64;
    }

    struct iovec output_buffers[output_size];
    for (int i = 0; i < output_size; i++) {
        output_buffers[i].iov_base = &result_buffer[i];
        output_buffers[i].iov_len = result_buffer[i].size();
    }

    output_buffers[0].iov_base = (char *)output_buffers[0].iov_base + last_writed_bytes;
    output_buffers[0].iov_len -= last_writed_bytes;

    int writed_buffers;
    for (writed_buffers = 0; writed_buffers < output_size; writed_buffers++) {
        last_writed_bytes =
            write(_socket, output_buffers[writed_buffers].iov_base, output_buffers[writed_buffers].iov_len);
        // error occured
        if (last_writed_bytes < 0) {
            alive = false;
            throw std::runtime_error(std::string(strerror(errno)));
        }
        // output buffer is filled up
        else if (!last_writed_bytes) {
            break;
        }
    }

    result_buffer.erase(result_buffer.begin(), result_buffer.begin() + writed_buffers);
    // all buffers are writed
    if (result_buffer.empty()) {
        _event.events ^= EPOLLOUT;
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
