//
//  scheduler.cpp
//  Processor Scheduler
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 9/13/26.
//

#include <queue>
#include <algorithm>
#include "scheduler.hpp"

std::queue<ProcessId_t> readyQ;
#define NUMBER_OF_CORES 8
ProcessId_t running_processes[NUMBER_OF_CORES];
int core = 0;
bool initialized = false;

void scheduler_init () {
    for (int i = 0; i < NUMBER_OF_CORES; i++)
        running_processes[i] = InvalidProcessId();
    initialized = true;
}

void CreateProcess(ProcessId_t pid) {
    // A new process has been created. Update the scheduler's data structures and decisions accordingly.
    SimOutput("CreateProcess(" + std::to_string(pid) + ")", 4);
    if (!initialized)
        scheduler_init();
    if(running_processes[core] == InvalidProcessId()) {
        running_processes[core] = pid;
        LoadContext(running_processes[core], core); // loads running onto core 'n'
        RunCore(core); // runs last loaded process
        core++;
        core = core % 8; // energy inefficient?
    }
    else {  // There is already a running process
        readyQ.push(pid);
    }
}

int getCore (ProcessId_t pid) {
    for (int i = 0; i < 8; i++) {
        if (running_processes[i] == pid)
            return i;
    }
    return -1;
}

bool isRunning (int core) {
    return running_processes[core] != InvalidProcessId();
}

void ExitProcess(ProcessId_t pid) {
    // Process finished running. Update the scheduler's data structures and decisions accordingly.
    int current_core = getCore (pid);
    if(current_core == -1) {
        ThrowException("A process that was not running is calling exit!!!");
    }
    if(!readyQ.empty()){
        running_processes[current_core] = readyQ.front();
        readyQ.pop();
        LoadContext(running_processes[current_core], current_core);
        RunCore(current_core);
    }
    else { // ATOMIC???
        running_processes[current_core] = InvalidProcessId();   // Nothing is running right now
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
    
    for (int i = 0; i < 8; i++) {
        if (isRunning(i)) {
            SaveContext(running_processes[i], i);
            readyQ.push(running_processes[i]);
            running_processes[i] = readyQ.front();
            readyQ.pop();
            LoadContext(running_processes[i], i);
            RunCore(i);
        } // else turn off
    }
}

void CStateTransitionComplete(CPUId_t core_id){
    
}

void SimulationComplete(Time_t now) {
    // Add any bookkeeping or statistics that you would want to collect. Program terminates after this function returns.
    std::cout << "Run stopped at " << FormatTime(now) << " after consuming " << GetTotalEnergyConsumed()/3600000000.0 << " kWh" << std::endl;
}
