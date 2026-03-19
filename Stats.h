#pragma once
#include <windows.h>
#include <pdh.h>


//STL
#include <string>
#include <string_view>
#include <thread>
#include <mutex>
#include <format>
#include <vector>
//lib
#pragma comment(lib, "pdh.lib")

#define FILETIME_2_ULL(ft) \
    ((((ULONGLONG)((ft).dwHighDateTime)) << 32) | ((ft).dwLowDateTime))






struct CPU_T {
    std::pair<bool, double> usage{ true, 0.0 };
    std::pair<bool, double> temp{ true, 0.0 };
    std::pair<bool, double> power{ true, 0.0 };
};

struct RAM_T {
    std::pair<bool, unsigned long long> total{ true, 0 };
    std::pair<bool, unsigned long long> usage{ true, 0 };
};
struct GPU_T {

    struct vramT {
        std::pair<bool, unsigned long long> total{ true, 0 };
        std::pair<bool, unsigned long long> proc{ true, 0 };

    };
    std::pair<bool, unsigned long long> usage{ true, 0 };
    std::pair<bool, unsigned long long> temp{ true, 0 };
    std::pair<bool, vramT > vram{ true, {} };


};

struct Render_T {
    std::pair<bool, unsigned long long> fps{ true, 0 };
    std::pair<bool, unsigned long long> dat{ true, 0 };
    std::pair<bool, std::string> API{ true, ""};

};
struct SharedData {
    char line1[128];
    char line2[128];
};


namespace RtkLoader {

    struct Req_ReadMsr {
        uint32_t index;   
        uint32_t hi; 
        uint32_t lo;   
    };
    HANDLE Drv = 0 ;

    bool loaded=false;

    bool Load(const std::string& path) {
        SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
        if (!hSCM) {
            std::cout << "err: RtkLoader::OpenSCManager " << GetLastError();
            return false;
        }

        SC_HANDLE hService = CreateServiceA(hSCM,"","",SERVICE_START | DELETE | SERVICE_STOP,SERVICE_KERNEL_DRIVER,SERVICE_DEMAND_START,SERVICE_ERROR_IGNORE, path.c_str(),nullptr, nullptr, nullptr, nullptr, nullptr);

        if (!hService) {
            hService = OpenServiceA(hSCM, "", SERVICE_START);
            if (!hService) {
                std::cout << "err: RtkLoader::OpenServiceA";
                CloseServiceHandle(hSCM);
                return false;
            }
        }

        if (!StartService(hService, 0, nullptr)) {
            if (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
                std::cout << "err: RtkLoader::StartService" << GetLastError();
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCM);
                return false;
            }
        }

        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return true;
    }

    uint64_t ReadMsr(uint32_t msrID) {

        //0x80002030 -> read msr
        //0x198 -> IA32_PERF_STATUS -> clock
        //0x19C -> IA32_THERM_STATUS -> temp (value >> 16) & 0x7F;
        //0x1A2 -> IA32_TEMPERATURE_TARGET -> TjMax 

        RtkLoader::Load("");

        if (!loaded) {

            Drv = CreateFileW(L"\\\\.\\", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (Drv == INVALID_HANDLE_VALUE) {
                std::cout << "CreateFileW: " << GetLastError();
                return 0;
            }
            loaded = true;
        }
        RtkLoader::Req_ReadMsr read;
        read.index = msrID;

        DWORD bytesReturned;

        BOOL ok = DeviceIoControl(Drv,0x80002030,&read,sizeof(read),&read,sizeof(read),&bytesReturned,nullptr);

        if (!ok) {
            std::cout << "RtkLoader::ReadMsr::DeviceIoControl: " << GetLastError();
            return 1;
        }

        uint64_t value = ((uint64_t)read.hi << 32) | read.lo;

 

        return value;
    }



    bool Unload() {
        SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!hSCM) return false;

        SC_HANDLE hService = OpenServiceW(hSCM, L"", SERVICE_STOP | DELETE);
        if (!hService) {
            CloseServiceHandle(hSCM);
            return false;
        }

        SERVICE_STATUS status;
        ControlService(hService, SERVICE_CONTROL_STOP, &status);

        DeleteService(hService);

        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);

        return true;
    }
}








struct monitor {
    std::mutex mtx;
    std::atomic<bool> Bloop = true;
    std::thread t1;
    PDH_HQUERY gpuQuery = nullptr;
    PDH_HCOUNTER gpuCounter = nullptr;
    bool gpuInit = false;

    monitor() {
        RtkLoader::Load((""));
        if (gpuInit) return;

        if (PdhOpenQuery(NULL, 0, &gpuQuery) != ERROR_SUCCESS)
            return;

        if (PdhAddEnglishCounterW(gpuQuery,L"\\GPU Engine(*)\\Utilization Percentage",0,&gpuCounter) != ERROR_SUCCESS)
            return;

        PdhCollectQueryData(gpuQuery);
        gpuInit = true;
    }


    ~monitor() {
        stop();
        RtkLoader::Unload();
    }

    struct Tdata {
        std::pair<bool, CPU_T> cpu{ true, CPU_T{} };
        std::pair<bool, RAM_T> ram{ true, RAM_T{} };
        std::pair<bool, GPU_T> gpu{ true, GPU_T{} };
        std::pair<bool, Render_T> render{ true, Render_T{} };


    } data;
    void start() {
        Bloop = true;
        t1 = std::thread(&monitor::loop, this);
    }
    void stop() {
        Bloop = false;
        if (t1.joinable())
            t1.join();
    }
    std::vector<std::string> to_string() {
        std::vector<std::string> out;

        {
            std::lock_guard<std::mutex> lock(mtx);



#pragma region CPU

            if (data.cpu.first) {
                auto& c = data.cpu.second;
                {
                    std::string line = "CPU: ";

                    if (c.usage.first)
                        line += std::format("{:.1f}% ", c.usage.second);

                    if (c.power.first)
                        line += std::format("{:.1f}GHz ", c.power.second / 1000.0);

                    if (c.temp.first)
                        line += std::format("{:.1f}°C ", c.temp.second);

                    out.push_back(line);
                }
            }
            if (data.gpu.first) {
                auto& g = data.gpu.second;
                std::string line = "GPU: ";
                {

                    if (g.usage.first) {
                        line += std::format("{}% ", g.usage.second);
                    }
                }
                out.push_back(line);

            }
#pragma endregion

        }
        return out;
    }

 

    double CpuUsage() {
        static bool initialized = false;
        static ULONGLONG lastIdle = 0, lastKernel = 0, lastUser = 0;

        FILETIME idleT, kernelT, userT;

        if (!GetSystemTimes(&idleT, &kernelT, &userT))
            return 0.0;

        ULONGLONG idle = FILETIME_2_ULL(idleT);
        ULONGLONG kernel = FILETIME_2_ULL(kernelT);
        ULONGLONG user = FILETIME_2_ULL(userT);

        if (!initialized) {
            lastIdle = idle;
            lastKernel = kernel;
            lastUser = user;
            initialized = true;
            return 0.0;
        }

        ULONGLONG idleD = idle - lastIdle;
        ULONGLONG kernelD = kernel - lastKernel;
        ULONGLONG userD = user - lastUser;

        lastIdle = idle;
        lastKernel = kernel;
        lastUser = user;

        ULONGLONG total = kernelD + userD;

        if (total == 0)
            return 0.0;

        double active = (double)(total - idleD);

        return (active / total) * 100.0;
    }

    double GpuUsage() {
        if (!gpuInit) return 0.0;

        PdhCollectQueryData(gpuQuery);

        DWORD size = 0, count = 0;

        PdhGetFormattedCounterArrayW(gpuCounter,PDH_FMT_DOUBLE,&size,&count,NULL);

        if (size == 0) return 0.0;

        std::vector<BYTE> buffer(size);

        auto items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buffer.data();

        if (PdhGetFormattedCounterArrayW(gpuCounter,PDH_FMT_DOUBLE,&size,&count,items) != ERROR_SUCCESS)
            return 0.0;

        double total = 0.0;

        for (DWORD i = 0; i < count; i++) {
            if (wcsstr(items[i].szName, L"engtype_3D")) {
                total += items[i].FmtValue.doubleValue;
            }
        }

        return total;

    }
    
    void loop() {
         while (Bloop) {
 
            MEMORYSTATUSEX mem;
            mem.dwLength = sizeof(mem);
            GlobalMemoryStatusEx(&mem);
            double cpu_use = 0.0;
            double Packclock = 0.0;
            double Cputemp = 0.0;

            double gpu_use = 0.0;

            if (data.cpu.first) {
                cpu_use  = CpuUsage();
            }
            if (data.cpu.second.power.first) {
                auto perf = RtkLoader::ReadMsr(0x198);
                uint32_t ratio = (perf >> 8) & 0xFF;
                Packclock = ratio * 100.0;
            }
            if (data.cpu.second.temp.first) {
                auto therm = RtkLoader::ReadMsr(0x19C);
                auto tjmax = RtkLoader::ReadMsr(0x1A2);
                uint32_t delta = (therm >> 16) & 0x7F;
                uint32_t tj = (tjmax >> 16) & 0xFF;
                Cputemp = tj - delta;

            }

            if (data.gpu.first) {
                gpu_use = GpuUsage();
            }
            
            {

                //package // cpu
                //-------------------------------------------------------

                std::lock_guard<std::mutex> lock(mtx);
                if (data.cpu.second.usage.first)
                    this->data.cpu.second.usage.second = cpu_use;

                if (data.cpu.second.temp.first)
                    this->data.cpu.second.temp.second = Cputemp;

                if (data.cpu.second.power.first)
                    this->data.cpu.second.power.second = Packclock;
                //-----------------------------------------------------------------------------------
                //gpu

                if (data.gpu.second.usage.first) {
                    data.gpu.second.usage.second = gpu_use;
                }

                //-----------------------------------------------------------------------------------
                //RAM

                if(data.ram.second.total.first)
                    this->data.ram.second.total.second = mem.ullTotalPhys;

                if (data.ram.second.usage.first)
                    this->data.ram.second.usage.second = mem.ullTotalPhys - mem.ullAvailPhys;
                //-----------------------------------------------------------------------------------


            }
            Sleep(1000);
        }

    }

    void readerCli() {
        monitor m;
        m.start();

        HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedData),"Local\\GengarTuner");

        SharedData* shm = (SharedData*)MapViewOfFile( hMap,FILE_MAP_ALL_ACCESS,0, 0,sizeof(SharedData));

        while (true) {
            auto lines = m.to_string();

            if (lines.size() > 0)
                strncpy_s(shm->line1, lines[0].c_str(), _TRUNCATE);

            if (lines.size() > 1)
                strncpy_s(shm->line2, lines[1].c_str(), _TRUNCATE);

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        m.stop();
    }

};
