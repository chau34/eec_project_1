//
//  scheduler.cpp
//  Processor Scheduler
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 9/13/26.
//

#include <queue>
#include <algorithm>
#include "scheduler.hpp"

#define NUMBER_OF_CORES 8

typedef struct core_info {
    CState_t c_state;
    ProcessId_t running;
    bool isTransitioning;
} CoreInfo;


std::queue<ProcessId_t> readyQ;

CoreInfo core_info[NUMBER_OF_CORES];
int core;
bool initialized = false;

void scheduler_init () {
    std::cout << "Init" << std::endl;
    core = 0;
    // Initialize core_info array, we have 8 cores
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        core_info[i].running = InvalidProcessId();
        core_info[i].c_state = C1;
        core_info[i].isTransitioning = false;
    }
    // Start low energy cores in low power state
    for (int i = 1; i < NUMBER_OF_CORES; i++) {
        SetCState (i, C6);
        core_info[i].c_state = C6;
    }
    initialized = true;
}

void CreateProcess(ProcessId_t pid) {
    // A new process has been created. Update the scheduler's data structures and decisions accordingly.
    SimOutput("CreateProcess(" + std::to_string(pid) + ")", 4);
    if (!initialized)
        scheduler_init();
    if(core_info[core].running == InvalidProcessId()) {
        if (core_info[core].c_state > C1 && !core_info[core].isTransitioning) {
            SetCState(core, C1);
            core_info[core].c_state = C1;
            core_info[core].isTransitioning = true;
            core_info[core].running = pid;
        } else {
            core_info[core].running = pid;
            LoadContext(core_info[core].running, core); // loads running onto core 'n'
            RunCore(core);
        }
        core++;
        core = core % 1; // energy inefficient?
    }
    else {  // There is already a running process
        readyQ.push(pid);
    }
}

int getCore (ProcessId_t pid) {
    for (int i = 0; i < 8; i++) {
        if (core_info[i].running == pid)
            return i;
    }
    return -1;
}

bool isRunning (int core) {
    return core_info[core].running != InvalidProcessId();
}

void ExitProcess(ProcessId_t pid) {
    // Process finished running. Update the scheduler's data structures and decisions accordingly.
    int current_core = getCore (pid);
    if(current_core == -1) {
        ThrowException("A process that was not running is calling exit!!!");
    }
    if(!readyQ.empty()){
        core_info[current_core].running = readyQ.front();
        readyQ.pop();
        LoadContext(core_info[current_core].running, current_core);
        RunCore(current_core);
    }
    else { // ATOMIC???
        core_info[current_core].running = InvalidProcessId();   // Nothing is running right now
    }
}

void TimerInterrupt(Time_t now) {
    bool currently_running = false;
    for (int i = 0; i < 8; i++) {
        if (isRunning(i))
            currently_running = true;
    }
    // You received a timer interrupt. This is where you want to execute scheduling decisions
    if(!currently_running)       // Nothing to do TODO: c7
        return;
    // Someone was running
    if(readyQ.empty())                      // We have a running process but no other processes are waiting
        return;
    
    // Would FIFO be better?
    // for (int i = 0; i < 8; i++) {
    //     if (isRunning(i) && !core_info[i].isTransitioning) {
    //         SaveContext(core_info[i].running, i);
    //         readyQ.push(core_info[i].running);
    //         core_info[i].running = readyQ.front();
    //         readyQ.pop();
    //         LoadContext(core_info[i].running, i);
    //         RunCore(i);
    //     } // else turn off
    // }
}

void CStateTransitionComplete(CPUId_t core_id){
    core_info[core_id].isTransitioning = false;
    LoadContext(core_info[core_id].running, core_id);
    RunCore(core_id);
}

void SimulationComplete(Time_t now) {
    // Add any bookkeeping or statistics that you would want to collect. Program terminates after this function returns.
    std::cout << "Run stopped at " << FormatTime(now) << " after consuming " << GetTotalEnergyConsumed()/3600000000.0 << " kWh" << std::endl;
}
