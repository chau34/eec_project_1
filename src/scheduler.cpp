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
    int idx;
} CoreInfo;


std::queue<ProcessId_t> readyQ;

CoreInfo core_info[NUMBER_OF_CORES];
bool initialized = false;

void reset_core (CoreInfo* core) {
    core->running = InvalidProcessId();
    core->c_state = C1;
    core->p_state = P0;
    core->isTransitioning = false;
    core->ticks = 0;
}

void update_core (CoreInfo* core, CState_t c_state, PState_t p_state, ProcessId_t pid) {
    /* Don't update any core that is actively transitioning */
    if (!core->isTransitioning) {
        core->running = pid;
        /* Don't update state if we're already there */
        if (p_state != core->p_state) {
            core->p_state = p_state;
            SetPState (core->idx, p_state);
        }
        if (c_state != core->c_state) {
            core->c_state = c_state;
            if (c_state == C6 || c_state == C1)
                core->isTransitioning = true;
            SetCState (core->idx, c_state);
        }
    }
}

void scheduler_init () {
    std::cout << "Init" << std::endl;

    CoreInfo* core;
    
    /* Initialize all cores */
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        core = &core_info[i];
        core->idx = i;
        reset_core (core);
        /* Start small cores in powered down state */
        if (i < NUMBER_OF_CORES / 2)
            update_core (core, C6, P0, InvalidProcessId());
    }
    initialized = true;
}

/* Returns if the core has nothing running, and is already in a powered on state while not transitioning */
bool free_core (CoreInfo* core) {
    return core->running == InvalidProcessId() && core->c_state <= C1 && !core->isTransitioning;
}

bool schedule_ideal (ProcessId_t pid) {
    CoreInfo* core;
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        core = &core_info[i];

        if (free_core(core)) {
            update_core (core, C1, P0, pid);
            LoadContext(pid, i);
            RunCore(i);
            return true;
        }
    }
    return false;
}

bool isRunning (int core) {
    return core_info[core].running != InvalidProcessId();
}

bool isValid (CoreInfo* core) {
    return !isRunning(core->idx) && !core->isTransitioning;
}

bool existsIdle() {
    for (int i = 0; i < NUMBER_OF_CORES; i++)
        if (isValid(&core_info[i]))
            return true;
    return false;
}

void CreateProcess(ProcessId_t pid) {
    // A new process has been created. Update the scheduler's data structures and decisions accordingly.
    SimOutput("CreateProcess(" + std::to_string(pid) + ")", 4);
    if (!initialized)
        scheduler_init();

    CoreInfo* core;

    /* Look for ideal core first */
    if (schedule_ideal (pid))
        return;

    /* All cores are running something, or we can batch */
    if (!existsIdle() || readyQ.size() < 400) { // TODO: Flipping existsIdle increases performance
        readyQ.push(pid);
        return;
    }
    
    /* We don't have an ideal core and our readyQ is large */
    core = NULL;
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        CoreInfo* candidate = &core_info[i];
        if (isValid(candidate)) {
            // Possible candidate, in a lower CState
            if (!core || (candidate->c_state < core->c_state)) {
                core = candidate;
            }
        }
    }
    
    if (core != NULL) {
        core->ticks = 0;
        update_core (core, C1, P0, pid);
    } else {
        readyQ.push(pid);
    }
    return;
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


int getCore (ProcessId_t pid) {
    for (int i = 0; i < 8; i++) {
        if (core_info[i].running == pid)
            return i;
    }
    return -1;
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