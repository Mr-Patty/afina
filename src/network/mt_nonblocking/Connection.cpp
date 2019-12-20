#include "Connection.h"

#include <iostream>
#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

// See Connection.h
void Connection::Start() {
    std::lock_guard<std::mutex> lock(mt);
    _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
    _event.data.fd = _socket;
    _logger->debug("Start connection on {} socket", _socket);

    command_to_execute.reset();
    argument_for_command.resize(0);
    parser.Reset();
    arg_remains = 0;

    cur_position = 0;
    old_readed_bytes = 0;
    answers.clear();
}

// See Connection.h
void Connection::OnError() {
    _logger->debug("Error on {} socket", _socket);
    is_Alive.store(false);
}

// See Connection.h
void Connection::OnClose() {
    _logger->debug("Close {} socket", _socket);
    is_Alive.store(false);
}

// See Connection.h
void Connection::DoRead() {
    _logger->debug("Read from {} socket", _socket);
    std::lock_guard<std::mutex> lock(mt);
    int client_socket = _socket;
    command_to_execute = nullptr;
    try {
        int readed_bytes = -1;
        while ((readed_bytes = read(_socket, client_buffer + old_readed_bytes, sizeof(client_buffer) - old_readed_bytes)) > 0) {
            _logger->debug("Got {} bytes from socket", readed_bytes);
            old_readed_bytes += readed_bytes;

            // Single block of data readed from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            while (old_readed_bytes > 0) {
                _logger->debug("Process {} bytes", old_readed_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer, old_readed_bytes, parsed)) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0) {
                            arg_remains += 2; // Зачем? Кажется с этим он неправильно парсит результат.
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, old_readed_bytes - parsed);
                        old_readed_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", old_readed_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(old_readed_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, old_readed_bytes - to_read);
                    arg_remains -= to_read;
                    old_readed_bytes -= to_read;
                }

                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");

                    std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);
                    // Send response
                    result += "\r\n";
                    if (answers.empty()) {
                        _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLOUT;
                    }
                    answers.push_back(result);

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }

        if (readed_bytes == 0) {
            _logger->debug("Connection closed");
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }
}

// See Connection.h
void Connection::DoWrite() {
    std::lock_guard<std::mutex> lock(mt);
    if (answers.empty()) {
        return;
    }
    _logger->debug("Write to {} socket", _socket);

    struct iovec iovecs[answers.size()];

//    iovecs[0].iov_len = answers[0].size() - cur_position;
//    iovecs[0].iov_base = &(answers[0][0]) + cur_position;
    int i = 0;
    for (auto &res : answers) {
        iovecs[i].iov_len = res.size();
        iovecs[i].iov_base = &(res[0]);
        ++i;
    }
    iovecs[0].iov_len -= cur_position;
    iovecs[0].iov_base = &(answers[0][0]) + cur_position;

    ssize_t written;
    if ((written = writev(_socket, iovecs, answers.size())) <= 0) {
        OnError();
    }
    cur_position += written;

    auto it = answers.begin();
    while (it != answers.end() && (cur_position >= it->size())) {
        cur_position -= it->size();
        it++;
    }

    answers.erase(answers.begin(), it);
    if (answers.empty()) {
        _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR;
    }
}

} // namespace MTnonblock
} // namespace Network
} // namespace Afina
