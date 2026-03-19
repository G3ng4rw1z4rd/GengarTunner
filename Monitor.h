#pragma once
#pragma once
#include <windows.h>
#include <utility>
#include <format>


struct Render_T {
    std::pair<bool, unsigned long long> fps{ true, 0 };
    std::pair<bool, std::string> data{ true, ""};

};
struct SharedData {
    char line1[128];
    char line2[128];
};



SharedData* readPipe() {
    HANDLE hMap = OpenFileMappingA(
        FILE_MAP_READ,
        FALSE,
        "Local\\GengarTuner"
    );

    SharedData* shm = (SharedData*)MapViewOfFile(hMap,FILE_MAP_READ,0, 0,sizeof(SharedData));

    
    return shm;
}