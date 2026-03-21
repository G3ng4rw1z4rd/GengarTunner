#include <windows.h>
#include <iostream>
#include "Stats.h"

int main() {
    monitor m;

    std::thread t1(&monitor::readerCli, &m);
    std::thread t2(&monitor::drawTaskbar, &m);

    t1.join();
    t2.join();
}
 

 
