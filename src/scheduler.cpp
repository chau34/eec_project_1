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

int getCore (ProcessId_t pid) {
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        if (core_info[i].running == pid)
            return i;
    }
    return -1;
}

void reset_core (CoreInfo* core) {
    core->running = InvalidProcessId();
    core->c_state = C1;
    core->p_state = P0;
    core->isTransitioning = false;
    core->ticks = 0;
}


// TODO: fix update_core because changing p states in here do not matter
void update_core (CoreInfo* core, CState_t c_state, PState_t p_state, ProcessId_t pid, bool transition) {
    /* Don't update any core that is actively transitioning */
    if (!core->isTransitioning) {
        core->running = pid;
        /* Don't update state if we're already there */
        // TODO: lwk not used because we should set after calling runcore()
        // maybe make a separate helper and call after runcore()
        // if (p_state != core->p_state) {
        //     core->p_state = p_state;
        //     SetPState (core->idx, p_state);
        // }
        if (c_state != core->c_state) {
            core->c_state = c_state;
            core->isTransitioning = transition;
            SetCState (core->idx, c_state);
        }
    }
}

template <typename State>
State inc_state (State state) {
    return (static_cast<State> ((static_cast<int> (state)) + 1));
}

void scheduler_init () {
    std::cout << "Init" << std::endl;

    CoreInfo* core;
    
    /* Initialize all cores */
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        core = &core_info[i];
        core->idx = i;
        reset_core (core);
        // /* Start small cores in powered down state */
        if (i < NUMBER_OF_CORES / 2)
            update_core (core, C4, P0, InvalidProcessId(), false);
            // update_core (core, C6, P0, InvalidProcessId());
    }
    initialized = true;
}

bool isRunning (int core) {
    return core_info[core].running != InvalidProcessId();
}

bool isValid (CoreInfo* core) {
    return !isRunning(core->idx) && !core->isTransitioning;
}

/* Returns if the core has nothing running, and is already in a powered on state while not transitioning */
bool free_core_thresh (CoreInfo* core, CState_t c_state) {
    return isValid(core) && core->c_state <= c_state;
}

bool schedule_ideal (ProcessId_t pid) {
    CoreInfo* core;
    for (int i = NUMBER_OF_CORES - 1; i >= 0; i--) {
        core = &core_info[i];

        if (free_core_thresh(core, C1)) {
            update_core (core, C1, P0, pid, false);
            LoadContext(pid, i);
            RunCore(i);
            core->p_state = P0;
            SetPState(core->idx, P0);
            return true;
        }
    }
    return false;
}

bool schedule_sleeping (ProcessId_t pid) {
    CoreInfo* core = NULL;
    for (int i = NUMBER_OF_CORES / 2; i < NUMBER_OF_CORES; i++) {
       
        CoreInfo* candidate = &core_info[i];
        if (isValid(candidate)) {
            // Possible small core candidate, in a lower CState
            if (!core || (candidate->c_state < core->c_state)) {
                core = candidate;
            }
        }
    }

    // can't find an available small core, wake up big core
    if (core == NULL) {
        for (int i = 0; i < NUMBER_OF_CORES / 2; i++) {
            CoreInfo* candidate = &core_info[i];
            if (isValid(candidate)) {
                // Possible big core candidate, in a lower CState
                if (!core || (candidate->c_state < core->c_state)) {
                    core = candidate;
                }
            }
        }    
    }
    
    if (core != NULL) {
        bool transition = (core->c_state >= C3);
        update_core (core, C1, core->p_state, pid, transition);
        if (!transition) {
            LoadContext(core->running, core->idx);
            RunCore(core->idx);
            core->p_state = P0;
            SetPState(core->idx, core->p_state); 
        }
        return true;
    }
    return false;
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

    /* Look for ideal core first */
    if (schedule_ideal (pid))
        return;

    /* All cores are running something, or we can batch */
    if (!existsIdle() || readyQ.size() < 100) { // TODO: Flipping existsIdle increases performance
        readyQ.push(pid);
        return;
    }
    
    /* We don't have an ideal core and our readyQ is large */
    if (!schedule_sleeping (pid)) {
        readyQ.push(pid);
    }
}

void ExitProcess(ProcessId_t pid) {
    // Process finished running. Update the scheduler's data structures and decisions accordingly.
    
    int current_core = getCore (pid);
    
    if (current_core == -1) {
        ThrowException("A process that was not running is calling exit!!!");
    }

    CoreInfo* core = &core_info[current_core];

    /* We have more work to do, keep running */
    if(!readyQ.empty()){
        // check if we're big core and there's not much work, go to sleep
        if (readyQ.size() < 100 && current_core < NUMBER_OF_CORES / 2)
            update_core (core, C2, core->p_state, InvalidProcessId(), false);
        else {
            update_core (core, core->c_state, P0, readyQ.front(), false);
            readyQ.pop();

            LoadContext(core_info[current_core].running, current_core);
            RunCore(current_core);
            core->p_state = P3;
            SetPState(core->idx, core->p_state); 
        }
    } else { /* We are out of work, we can go to sleep */
        update_core (core, C2, core->p_state, InvalidProcessId(), false);
    }
}

void preempt (CoreInfo* core) {
    core->p_state = P3;
    SetPState(core->idx, core->p_state); 
    SaveContext(core->running, core->idx);
    readyQ.push(core->running);
    core->running = readyQ.front();
    readyQ.pop();
    LoadContext(core->running, core->idx);
    RunCore(core->idx);
}

bool preemptable (CoreInfo* core) {
    return isRunning(core->idx) && !core->isTransitioning;
}

bool shouldPreempt (CoreInfo* core) {
    return preemptable(core) && readyQ.size() > 10;
}

bool shouldLowerPStates (CoreInfo* core) {
    return isRunning(core->idx) && !core->isTransitioning && readyQ.empty() && core->p_state < P4;
}

void update_cores () {
    CoreInfo* core;
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        core = &core_info[i];
        /* Lower C_States for Idle Cores */
        if (free_core_thresh(core, C4)) {
            CState_t c_state = inc_state (core->c_state);
            if (c_state == C5)
                c_state = C6;
            bool transition = c_state == C6;
            update_core (core, c_state, core->p_state, core->running, transition);
        } 

        /* Preempt if our readyQ is growing larger than size 10 */ 
        else if (shouldPreempt (core)) {
            // TODO: Investigate this line, p_state not honored, best is p4?
            preempt (core);
        } 

        /* Nothing in Queue, Lower P_State */
        // TODO: this runs worse so I comment it out... I think we should keep it at P0/P3
        // else if (shouldLowerPStates (core)) {
        //     update_core (core, core->c_state, inc_state (core->p_state), core->running, false);
        // }
    }
}

void debugPrinting() {
    std::cout << "rQ: " << readyQ.size() << std::endl;
    CoreInfo* core;
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        core = &core_info[i];
        std::cout << "C:" << i << " CS:" << core->c_state << " PS:" << core->p_state << " trans: " << core->isTransitioning << " | ";
    }
    std::cout << std::endl;
}

CoreInfo* getRunningLarge() {
    for (int i = 0; i < NUMBER_OF_CORES / 2; i++) {
        CoreInfo* core = &core_info[i];
        if (preemptable(core))
            return core;
    }
    return NULL;
}

CoreInfo* getIdleSmall() {
    for (int i = NUMBER_OF_CORES / 2; i < NUMBER_OF_CORES; i++) {
        CoreInfo* core = &core_info[i];
        if (isValid(core))
            return core;
    }
    return NULL;
}

void load_balancing () {
    if (readyQ.size() < 10) {
        // Check how many cores are running where
        int small_cores = 0;
        int large_cores = 0;
        
        for (int i = 0; i < NUMBER_OF_CORES / 2; i++) {
            if (preemptable(&core_info[i])) {
                large_cores++;
            }
        }
        for (int i = NUMBER_OF_CORES / 2; i < NUMBER_OF_CORES; i++) {
            if (isValid(&core_info[i])) {
                small_cores++;
            }
        }
        int swaps = large_cores > small_cores ? small_cores : large_cores;
        for (int i = 0; i < swaps; i++) {
            CoreInfo* large = getRunningLarge();
            CoreInfo* small = getIdleSmall();
            if (large && small) {
                ProcessId_t pid = large->running;
                // Errors here
                SaveContext(pid, large->idx);
                update_core(large, C2, large->p_state, InvalidProcessId(), false);
                bool transition = (small->c_state >= C3);
                update_core(small, C1, P0, pid, transition);
            }
        }
    }
}

// return how many cores to run upon ratio of cores and work being 1 to 20 or less.
// didn't change energy when I last run it... but I feel like this should be done
int enoughCores() {
    if (readyQ.size() == 0)
        return 0;
    int awakeCount = 0;
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        // make sure we don't overwake cores if it's transitioning to C1 to run
        if (isRunning(i) || (core_info[i].isTransitioning && core_info[i].c_state == C1))
            awakeCount++;
    }
    if (awakeCount == 0)
        return 1;
    if ((readyQ.size() / awakeCount) > 20)
        return (readyQ.size() / 20) - awakeCount;
    return 0;
}

void wake_cores () {
    int wakeup = enoughCores();
    for (int i = 0; i < wakeup; i++)
        if (schedule_sleeping(readyQ.front()))
            readyQ.pop();
        else
            return;
}

void TimerInterrupt(Time_t now) {
    if (!initialized) {
        scheduler_init();
    }
        
    if (initialized) {
        // TODO: can optimize further by checking ratio and wake up more than one core
        // debugPrinting(); // Uncomment to print debug
        wake_cores ();
        // load_balancing (); // not buggy no more, but doesn't cause any change because load balance on exit process
        update_cores ();
    }
}

/*
 * Either from {C3, C4} → {C0, C1, C2} or anywhere to {C6, C7}
 */
void CStateTransitionComplete(CPUId_t core_id){
    CoreInfo* core = &core_info[core_id];
    core->isTransitioning = false;
    if (core->c_state == C1 && core->running != InvalidProcessId()) {
        LoadContext(core->running, core_id);
        RunCore(core_id);
        core->p_state = P0;
        SetPState(core->idx, core->p_state); 
    }
}

void SimulationComplete(Time_t now) {
    // Add any bookkeeping or statistics that you would want to collect. Program terminates after this function returns.
    std::cout << "Run stopped at " << FormatTime(now) << " after consuming " << GetTotalEnergyConsumed()/3600000000.0 << " kWh" << std::endl;
}