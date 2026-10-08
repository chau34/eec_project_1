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
    PState_t p_state;
    ProcessId_t running;
    bool isTransitioning;
    int ticks;
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
        core_info[i].ticks = 0;
        core_info[i].p_state = P0;
    }
    // Start low energy cores in low power state
    for (int i = 0; i < NUMBER_OF_CORES / 2; i++) {
        core_info[i].isTransitioning = true;
        core_info[i].c_state = C6;
        SetCState (i, C6);
    }
    initialized = true;
}

bool wakeBestAndRun(ProcessId_t pid) {
    CoreInfo* ideal = NULL;
    int idx = 0;
    for (int i = NUMBER_OF_CORES - 1; i >= 0; i--) {
        CoreInfo* current = &core_info[i];
        if (current->running == InvalidProcessId() && !current->isTransitioning) {
            // Possible candidate, in a lower CState
            if (!ideal || (current->c_state < ideal->c_state)) {
                ideal = current;
                idx = i;
            }
        }
    }
    if (ideal != NULL) {
        core_info[idx].c_state = C1;
        core_info[idx].isTransitioning = true;
        core_info[idx].running = pid;
        core_info[idx].ticks = 0;
        SetCState(idx, C1);
        return true;
    }
    return false;
}

void CreateProcess(ProcessId_t pid) {
    // A new process has been created. Update the scheduler's data structures and decisions accordingly.
    SimOutput("CreateProcess(" + std::to_string(pid) + ")", 4);
    if (!initialized)
        scheduler_init();

    bool allRunning = true;

    // Free core in C1 to use (ideal)
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        CoreInfo* current = &core_info[i];
        if (current->running == InvalidProcessId())
            allRunning = false;
        if (current->running == InvalidProcessId() && current->c_state <= C1 && !current->isTransitioning) {
            current->running = pid;
            LoadContext(current->running, i);
            // current->p_state = P0;
            // SetPState(i, P0);
            RunCore(i);
            return;
        }
    }
    // All cores are running something, need to push to ready queue
    if (allRunning || readyQ.size() < 10) {
        readyQ.push(pid);
        return;
    }
    
    // There is no ideal core and we have a lot to run, find one to wakeup
    // TODO: Learn abt c and p state
    
    if (!wakeBestAndRun(pid)) {
        readyQ.push(pid);
    }
    return;
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
    } else { // ATOMIC???
        core_info[current_core].running = InvalidProcessId();   // Nothing is running right now
        // core_info[current_core].p_state = P0;
        // SetPState(current_core , P0);
        core_info[current_core].c_state = C2;
        SetCState(current_core, C2);
    }
}

// return how many cores to run upon ratio of cores and work being 1 to 20 or less.
// didn't change energy when I last run it... but I feel like this should be done
int enoughCores() {
    if (readyQ.size() == 0)
        return 0;
    int awakeCount = 0;
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        if (isRunning(i))
            awakeCount++;
    }
    if (awakeCount == 0)
        return 1;
    if ((readyQ.size() / awakeCount) > 20)
        return (readyQ.size() / 20) - awakeCount;
    return 0;
}

void TimerInterrupt(Time_t now) {
    // std::cout << readyQ.size() << std::endl;
    if (initialized) {
        // TODO: can optimize further by checking ratio and wake up more than one core
        int wakeup = enoughCores();
        for (int i = 0; i < wakeup; i++) {
            if (wakeBestAndRun(readyQ.front())) {
                readyQ.pop();
            } else {
                break;
            }
        }
        for (int i = 0; i < NUMBER_OF_CORES; i++) {
            CoreInfo* current = &core_info[i];
            // lower c states for idle cores
            if (!isRunning(i) && current->c_state < C6 && !current->isTransitioning) {
                current->c_state = (static_cast<CState_t> ((static_cast<int> (current->c_state)) + 1));
                if (current->c_state == C5)
                    current->c_state = C6;
                if (current->c_state == C6)
                    current->isTransitioning = true;
                SetCState(i, current->c_state);
            // Preempt if our readyQ is growing larger than size 10
            } else if (isRunning(i) && !current->isTransitioning && readyQ.size() > 10) {
                // std::cout << current->p_state << std::endl;
                // TODO: investigate this line, basically our pstate kept didn't get honored until here
                // all the other p states = 0 doens't change energy at all
                // it's best run at p4???
                SetPState(i, current->p_state); 
                SaveContext(current->running, i);
                readyQ.push(current->running);
                current->running = readyQ.front();
                readyQ.pop();
                LoadContext(current->running, i);
                RunCore(i);
            // nothing in queue, lower p states
            } else if (isRunning(i) && !current->isTransitioning && readyQ.size() == 0 && current->p_state < P4) {
                current->p_state = (static_cast<PState_t> ((static_cast<int> (current->p_state)) + 1));
                SetPState(i, current->p_state);
            }
        }
        
    }
}

/*
 * Either from {C3, C4} → {C0, C1, C2} or anywhere to {C6, C7}
 */
void CStateTransitionComplete(CPUId_t core_id){
    // if (core_info[core_id].c_state == C1)
    //     std::cout << "Here2" << std::endl;
    core_info[core_id].isTransitioning = false;
    if (core_info[core_id].c_state == C1) {
        LoadContext(core_info[core_id].running, core_id);
        RunCore(core_id);
    }
}

void SimulationComplete(Time_t now) {
    // Add any bookkeeping or statistics that you would want to collect. Program terminates after this function returns.
    std::cout << "Run stopped at " << FormatTime(now) << " after consuming " << GetTotalEnergyConsumed()/3600000000.0 << " kWh" << std::endl;
}