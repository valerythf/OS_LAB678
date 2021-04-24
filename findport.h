#pragma once

#include <zmqpp/zmqpp.hpp>
#include <string>

#define check(VAL, BADVAL, MSG) if (VAL == BADVAL) { perror(MSG); exit(1); }

const std::string host = "tcp://127.0.0.1:";

int try_bind(zmqpp::socket &socket) {
    static unsigned int port(49152);          // "Эфемерные" или динамические порты. Назначаются только на время соединения, после завершения сеанса становится свободным.
    while (true) {
        try {
             std::cout << "trying " << port;
            socket.bind(host + std::to_string(port));             // Привязка сокета к порту
        }
        catch (zmqpp::zmq_internal_exception &ex) {
            ++port;
             std::cout << std::endl;
            continue;
        }
         std::cout << " ok" << std::endl;
        return port++;
    }
}

enum class action : int {
    fork, exit, unbind_front, unbind_back, rebind_front, rebind_back, start,stop,time,test,done
};
