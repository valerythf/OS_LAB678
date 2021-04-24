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

zmqpp::context context;
zmqpp::socket req_sock(context, zmqpp::socket_type::push);


class myTimer_t{
private:
    size_t time_start;
    size_t timer_wait{};
public:
    myTimer_t(const size_t new_timer_wait): timer_wait(new_timer_wait), time_start(clock()){}
    myTimer_t(): time_start(0){}
    ~myTimer_t()=default;

    void Start(const size_t new_timer_wait){  // запоминание начального времени
        timer_wait = new_timer_wait;
        time_start = (size_t)clock();
    }

    size_t Time(){
        return time_start + timer_wait - (size_t)clock(); // количество прошедшего времени
    }
};




int timerID;
std::string req_port;



void* awake (void* arg){                               // awake - разбудить через arg миллисекунд
    size_t* wait = reinterpret_cast<size_t*>(arg);
    usleep((*wait) * 1000);                                     // Приостанавливает процесс на wait секунд, таким образом, получается таймер
    std::cout << "OK: timer out" << std::endl;
    zmqpp::message done_msg;                             // формируем message
    done_msg << timerID << static_cast<int>(action::done);
    std::cout << " the timerID =  " << timerID << "  action is ==  " << static_cast<int>(action::done);
    req_sock.send(done_msg);                                 // to front_in_port
    std::cout << "message was sent" << std::endl;
    return NULL;
}

int main(int argc, char* argv[]){
    std::string timer_port = argv[1];
    req_port = argv[2];
    zmqpp::socket timer_sock(context, zmqpp::socket_type::pull);        // Принимающий сокет (Передающий сокет задан в client)
    timer_sock.connect(host + timer_port);                // порт для сокера timer_sock биндится в client
    std::cout << "TIMER CONNECTED TO ID = " << timer_port << std::endl;
    req_sock.connect(host + req_port);
    std::cout << "REQ_SOCK CONNECTED TO ID = " << req_port << std::endl;
    myTimer_t myTimer;
    pthread_t awake_timer;
    std::cout << "im timer" << std::endl;

    while(true){
        int act;
        zmqpp::message msg;

        timer_sock.receive(msg);
        msg >> timerID >> act;
        std::cout << "i get message" << std::endl << "action = " << act << " timerID is  "
                  << timerID << std::endl;

        switch (static_cast<action>(act)) {
            case action::start: {                      // Включаем таймер
                size_t wait;
                msg >> wait;
                myTimer.Start(wait);
                check(pthread_create(&awake_timer, NULL, awake, (void *) &wait),-1,"pthread_create error"); //Аргументы: указатель на поток. Аттрибуты (по умолчанию = NULL). awake – функция, которая будет выполняться в новом потоке. arg – это аргументы, которые будут переданы функции. 

                if(pthread_detach(awake_timer) != 0){
                    perror("pthread_detach error");
                }
                std::cout << "OK: timer " << timerID << " started" << std::endl;
                break;
            }

            case action::stop: {                             // Выключаем таймер
                req_sock.disconnect(host + req_port);
                req_sock.close();
                timer_sock.disconnect(host + timer_port);
                timer_sock.close();
                pthread_cancel(awake_timer);
                std::cout << "OK: Timer " << timerID << " stopped" << std::endl;
                exit(0);
            }

            case action::time: {                     // Посмотреть оставшееся время
                std::cout << "im in timer's time" << std::endl;
                auto to_wait = myTimer.Time();
                std::cout << "OK: " << to_wait << " left" << std::endl;
                break;
            }

            case action::exit: {
                req_sock.disconnect(host + req_port);
                req_sock.close();
                timer_sock.disconnect(host + timer_port);
                timer_sock.close();
                std::cout << "OK: timer " << timerID << " closed" << std::endl;
                exit(0);
                return 0;
            }

            default: {}
        }

    }
}
