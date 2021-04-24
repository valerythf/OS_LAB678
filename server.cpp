#include <unistd.h>
#include <pthread.h>
#include <zmqpp/zmqpp.hpp>
#include <iostream>
#include <list>
#include <string>
#include <signal.h>
#include <sstream>


#include "findport.h"

struct nodes{
    int id;
    std::list<nodes> bros;
};

using line_t = std::pair <nodes, std::pair<//changed
        std::pair < zmqpp::socket, std::string>, std::pair<zmqpp::socket, std::string>>>;
std::list <line_t> network;

class node_coord {
private:
    std::pair <std::list<line_t>::iterator, nodes> data;

public:
    node_coord(std::list<line_t>::iterator &i1,nodes &i2) : data(i1, i2) {}

    node_coord(std::list<line_t>::iterator &&i1,nodes &&i2) : data(i1, i2) {}

    node_coord(node_coord &other) : data(other.data) {}

    node_coord(node_coord &&other) : data(other.data) {}

    std::list<line_t>::iterator &line() {
        return data.first;
    }

    nodes &node() {
        return data.second;
    }

    int &id() {
        return data.second.id;
    }

    int &parent() {
        // main parent
        return data.first->first.id;
    }

    zmqpp::socket &out_sock() {
        return data.first->second.first.first;
    }

    zmqpp::socket &ping_sock() {
        return data.first->second.second.first;
    }

    std::string &out_port() {
        return data.first->second.first.second;
    }

    std::string &ping_port() {
        return data.first->second.second.second;
    }
};

zmqpp::context context;

std::list<nodes>::iterator find_node(nodes& b_it, int id){
    auto result = b_it.bros.end();
    for(auto iter = b_it.bros.begin(); iter != b_it.bros.end() && b_it.bros.size() != 0; ++iter){
        if(iter->id == id){
            return iter;
        }

        result = find_node(*iter,id);
        if(result != iter->bros.end() && result->id == id){
            return result;
        }

        result = b_it.bros.end();
    }
    return result;
}

node_coord find_node(int id){
    for(auto b_it = network.begin(); b_it != network.end(); ++b_it){
        if(b_it->first.id == id){
            return {b_it, b_it->first};
        }

        auto result = find_node(b_it->first,id);
        if(result != b_it->first.bros.end() && result->id == id){
            return {b_it,*result};
        }
    }
    return {network.end(), nodes()};
}

void* result_waiter(void* arg){
    zmqpp::socket* in_sock = reinterpret_cast<zmqpp::socket*> (arg);
    zmqpp::message msg;
    int act, rid;

    while(*in_sock){
        in_sock->receive(msg);
        msg >> rid >> act;

        switch(static_cast<action>(act)){
            case action::test: {
                std::cout << "Hello " << rid << std::endl;
                break;
            }

            case action::fork: {
                int pid;
                msg >> pid;
                std::cout << "OK: " << pid << std::endl;
                break;
            }

            case action::exit: {
                node_coord node = find_node(rid);
            }

            default: {}
        }
    }
    return NULL;
}


int main(){
    zmqpp::socket in_sock(context, zmqpp::socket_type::pull);
    std::string in_port = std::to_string(try_bind(in_sock));
    std::cout << "IN_PORT ID = " << in_port << std::endl;

    zmqpp::socket bridge(context,zmqpp::socket_type::rep);
    std::string bridge_port = std::to_string(try_bind(bridge));
    std::cout << "BRIDGE ID = " << bridge_port << std::endl;


    //проверка создание потока
    pthread_t result_waiter_id;
    check(pthread_create(&result_waiter_id, NULL,result_waiter, (void*) &in_sock),-1, "pthread_create failed");

    if(pthread_detach(result_waiter_id) != 0){
        perror("detach error");
    }

    std::string command;
    while(std::cout << "> " && std::cin >> command && command != "exit"){
        if(command == "create"){
            int id, parent;
            std::cin >> id >> parent;

            if (id == -1 || find_node(id).line() != network.end()) {       // проверка, существует ли вершина
                std::cerr << "Error: Already exists" << std::endl;
                continue;
            }

            if (parent == -1) {                            //создание узла родителя
                network.push_back({nodes(), std::make_pair(std::make_pair(zmqpp::socket(context, zmqpp::socket_type::push), ""), std::make_pair(zmqpp::socket(context, zmqpp::socket_type::req), ""))});
                network.back().first.id = id;
                node_coord node(--network.end(), network.back().first);

                int pid = fork();
                check(pid, -1, "fork error");
                if (pid == 0) {
                    check(execl("client", "client", std::to_string(id).c_str(), std::to_string(-1).c_str(), bridge_port.c_str(), NULL), -1, "execl error");
                }

                zmqpp::message ports;
                bridge.receive(ports);

                ports >> node.out_port();
                node.out_sock().connect(host + node.out_port());
                std::cout << "OUT SOCKET FOR TIMER ID = " << node.out_port() << std::endl;

                ports >> node.ping_port();
                node.ping_sock().connect(host + node.ping_port());
                node.ping_sock().set(zmqpp::socket_option::receive_timeout, 1000);

                ports.pop_back();
                ports.pop_back();
                ports << in_port;
                bridge.send(ports);
            }

            else {
                node_coord parent_node = find_node(parent);
                if (parent_node.line() == network.end()) {
                    std::cerr << "Error: Parent not found" << std::endl;
                    continue;
                }

                parent_node.line()->first.bros.push_back({id,std::list<nodes>()});

                zmqpp::message msg;
                msg << parent << static_cast<int>(action::fork) << id;
                parent_node.out_sock().send(msg);
            }
        }

        if(command == "exec"){
            int id;
            std::cin >> id;

            node_coord node = find_node(id);

            if (node.line() == network.end()) {
                std::cerr << "Error: Not found" << std::endl;
                continue;
            }

            std::string act;
            std::cin >> act;
            zmqpp::message msg;
            msg << id;
            if(act == "start"){
                std::cout << "im in client's start" << std::endl;
                size_t wait;
                std::cin >> wait;
                msg  << static_cast<int>(action::start) << wait;
                node.out_sock().send(msg);
            }
            else if(act == "stop"){
                msg << static_cast<int>(action::stop);
                node.out_sock().send(msg);
            }
            else if(act == "time"){
                msg << static_cast<int>(action::time);
                node.out_sock().send(msg);
            }
        }

        if(command == "test"){
            int pid = fork();
            if(pid == 0){
                execl("client", "client", bridge_port.c_str(), NULL);
            }
            zmqpp::message msg;
            bridge.receive(msg);
            int id;
            msg >> id;
            std::cout << id << std::endl;

            msg.pop_back();
            msg.pop_back();

            msg << "im done";

            bridge.send(msg);
        }
    }

    return 0;
}



