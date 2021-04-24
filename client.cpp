#include <unistd.h>
#include <pthread.h>
#include <zmqpp/zmqpp.hpp>
#include <iostream>
#include <string>
#include <memory>
#include <algorithm>
#include <signal.h>
#include <map>

#include "findport.h"
//#define DEBUG

int id = -1, next_id = -1, prev_id = -1;
std::unique_ptr<zmqpp::socket> front_in(nullptr), front_out(nullptr), front_ping(nullptr),
        back_in(nullptr), back_out(nullptr), back_ping(nullptr);
std::string back_in_port, back_out_port, back_ping_port, front_ping_port;
zmqpp::context context;


int main(int argc, char* argv[]){
    assert(argc >= 4);
    id = strtoul(argv[1], nullptr, 10);
    prev_id = strtoul(argv[2], nullptr, 10);
    std::string bridge_port(argv[3]);
    // std::cout << getpid() << " " << id << std::endl;

    zmqpp::message msg;

    zmqpp::socket bridge(context, zmqpp::socket_type::req);
    bridge.connect(host + bridge_port);

    front_in.reset(new zmqpp::socket(context, zmqpp::socket_type::pull));
    std::string front_in_port = std::to_string(try_bind(*front_in));
    std::cout << "FRONT_IN_PORT ID = " << front_in_port << std::endl;

    front_ping.reset(new zmqpp::socket(context, zmqpp::socket_type::rep));
    front_ping_port = std::to_string(try_bind(*front_ping));
    std::cout << "FRONT_PING_PORT ID = " << front_ping_port << std::endl;

    msg << front_in_port << front_ping_port;
    bridge.send(msg);
    std::cout << "Message " << front_in_port << " " << front_ping_port << " is sent to " << bridge_port << std::endl;

    bridge.receive(msg);
    

    std::string front_out_port;
    msg >> front_out_port;
    std::cout << "Message (front_out_port) " << front_out_port << " is received from " << bridge_port << std::endl;
    
    front_out.reset(new zmqpp::socket(context, zmqpp::socket_type::push));
    front_out->connect(host + front_out_port);

    bridge.disconnect(host + bridge_port);
    bridge.close();
    bridge = zmqpp::socket(context, zmqpp::socket_type::rep);
    bridge_port = std::to_string(try_bind(bridge));
    std::cout << "BRIDGE ID = " << bridge_port << std::endl;

    {
        zmqpp::message ans;
        ans << id << static_cast<int>(action::fork) << getpid();
        front_out->send(ans);
    }


    int act;
    int timerID;

    zmqpp::socket timer_sock(context, zmqpp::socket_type::push);
    std::string timer_port = std::to_string(try_bind(timer_sock));
    std::cout << "TIMER SOCKET ID = " << timer_port << std::endl;
    bool timer_started = false;
    while (true) {
        if (!front_in->receive(msg)) {
            perror("");
        }

        msg >> timerID;

        if (timerID != id) {
            back_out->send(msg);
            continue;
        }

        msg >> act;

        switch (static_cast<action>(act)) {
            case action::fork: {
                int cid;
                msg >> cid;
             	{
                    next_id = cid;

                    int pid = fork();
                    check(pid, -1, "fork error");
                    if (pid == 0) {
                        check(execl("client", "client", std::to_string(cid).c_str(), std::to_string(id).c_str(),
                                    bridge_port.c_str(), NULL), -1, "execl error");
                    }


                    back_in.reset(new zmqpp::socket(context, zmqpp::socket_type::pull));
                    back_in_port = std::to_string(try_bind(*back_in));
                    std::cout << "BACK_IN ID = " << back_in_port << std::endl;

                    zmqpp::message ports;
                    bridge.receive(ports);

                    ports >> back_out_port >> back_ping_port;
                    ports.pop_back();
                    ports.pop_back();

                    back_out.reset(new zmqpp::socket(context, zmqpp::socket_type::push));
                    back_ping.reset(new zmqpp::socket(context, zmqpp::socket_type::req));
                    back_ping->set(zmqpp::socket_option::receive_timeout, 1000);

                    back_out->connect(host + back_out_port);
                    back_ping->connect(host + back_ping_port);

                    ports << back_in_port;
                    bridge.send(ports);
                }
                std::cout << "im in fork" << std::endl;
                break;
            }

            case action::done: {
                std::cout << "im in client's done" << std::endl;
                zmqpp::message exit_msg;
                exit_msg << timerID << static_cast<int>(action::exit);
                timer_sock.send(exit_msg);
                std::cout << "message (DONE) was sent to adress = " << timer_port << std::endl;
                timer_started = false;
                break;
            }

            case action::start: {
                if(timer_started){
                    std::cout << "timer already started" << std::endl;
                    break;
                }	

                int pid;
                pid = fork();
                check(pid,-1,"fork error");
                if(pid == 0){
                    check(execl("timer", "timer",timer_port.c_str(), front_in_port.c_str(), NULL),
                          -1,
                          "execl error");
                }
                size_t wait;
                msg >> wait;

                zmqpp::message timer_msg;
                timer_msg << timerID << static_cast<int>(action::start) << wait;
                timer_sock.send(timer_msg);

                timer_started = true;
                std::cout << "message (START) sent to adress " << timer_port << std::endl;

                break;
            }

            case action::time: {
                std::cout << "im in client's time" << std::endl;
                if(!timer_started){
                    std::cout << "You had not started timer" << std::endl;
                    break;
                }
                std::cout << "trying to send msg" << std::endl;

                zmqpp::message timer_msg;
                timer_msg << timerID << static_cast<int>(action::time) ;
                std::cout << "msg = " << timer_sock.send(timer_msg) << std::endl;
                break;
            }

            case action::stop: {
                if(!timer_started){
                    std::cout << "Please start timer" << std::endl;
                    break;
                }

                zmqpp::message timer_msg;
                timer_msg << timerID << static_cast<int>(action::stop) ;
                timer_sock.send(timer_msg);
                timer_started = false;
                break;
            }

            default:{}
        }
    }


    return 0;
}
