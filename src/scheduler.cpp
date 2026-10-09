//
//  scheduler.cpp
//  Processor Scheduler
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 9/13/26.
//

#include <queue>
#include <algorithm>
#include "scheduler.hpp"

/* Number of cores as defined in specs, first 4 are big, last 4 are small */
#define NUMBER_OF_CORES 8
/* Threshold for optimal size of readyQ for batching */
#define READY_Q_THRESH 100
/* Optimal ratio of processes per core */
#define CORE_RATIO 20
/* Defines the threshold where cores change from big->small */
#define CORE_SIZE_CHANGE 4

/* Core Info struct containing state information for the core */
typedef struct core_info {
    CState_t c_state;
    PState_t p_state;
    ProcessId_t running;
    bool isTransitioning;
    int idx;
} CoreInfo;


std::queue<ProcessId_t> readyQ;

/* Static array containing core_info structs to easily manipulate cores */
CoreInfo core_info[NUMBER_OF_CORES];

/* Initialization variable so we don't start scheduling before cores are set up */
bool initialized = false;

/* Gets core index from running pid */
int getCore (ProcessId_t pid) {
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        if (core_info[i].running == pid)
            return i;
    }
    return -1;
}

/* Used in init, sets all core state values to null equivalent */
void reset_core (CoreInfo* core) {
    core->running = InvalidProcessId();
    core->c_state = C1;
    core->p_state = P0;
    core->isTransitioning = false;
}

/* Helper function to update all states of a core at once safely */
void update_core (CoreInfo* core, CState_t c_state, ProcessId_t pid, bool transition) {
    /* Don't update any core that is actively transitioning */
    if (!core->isTransitioning) {
        core->running = pid;
        /* Don't update state if we're already there */
        if (c_state != core->c_state) {
            core->c_state = c_state;
            core->isTransitioning = transition;
            SetCState (core->idx, c_state);
        }
    }
}

/* Updates p state of core to p_state */
void updatePState(CoreInfo* core, PState_t p_state) {
    core->p_state = p_state;
    SetPState(core->idx, p_state);
}

/* Increment function for both state enum types */
template <typename State>
State inc_state (State state) {
    return (static_cast<State> ((static_cast<int> (state)) + 1));
}

/* Initializes all cores starting big cores in a powered down state */
void scheduler_init () {
    std::cout << "Init" << std::endl;

    CoreInfo* core;
    
    /* Initialize all cores */
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        core = &core_info[i];
        core->idx = i;
        reset_core (core);
        /* Start big cores in powered down state */
        if (i < CORE_SIZE_CHANGE)
            update_core (core, C4, InvalidProcessId(), false);
    }
    initialized = true;
}

/* Helper to check if the core at this index has a running pid */
bool isRunning (int core) {
    return core_info[core].running != InvalidProcessId();
}

/* Hekper to check if the current core can be scheduled on */
bool isValid (CoreInfo* core) {
    return !isRunning(core->idx) && !core->isTransitioning;
}

/* 
 *  Returns if the core has nothing running, and is already in a powered 
 *  on state while not transitioning under a certain threshold of c_state
 */
bool free_core_thresh (CoreInfo* core, CState_t c_state) {
    return isValid(core) && core->c_state <= c_state;
}

bool schedule_ideal (ProcessId_t pid) {
    CoreInfo* core;
    for (int i = NUMBER_OF_CORES - 1; i >= 0; i--) {
        core = &core_info[i];

        if (free_core_thresh(core, C1)) {
            update_core (core, C1, pid, false);
            LoadContext(pid, i);
            RunCore(i);
            updatePState(core, P0);
            return true;
        }
    }
    return false;
}

/* Schedules this pid on a sleeping core and waking it up, returns false if not possible */
bool schedule_sleeping (ProcessId_t pid) {
    CoreInfo* core = NULL;
    /* Check all small cores first */
    for (int i = CORE_SIZE_CHANGE; i < NUMBER_OF_CORES; i++) {
       
        CoreInfo* candidate = &core_info[i];
        if (isValid(candidate)) {
            /* Possible small core candidate, in lowest CState */
            if (!core || (candidate->c_state < core->c_state)) {
                core = candidate;
            }
        }
    }

    /* Can't find an available small core, wake up big core */
    if (core == NULL) {
        for (int i = 0; i < CORE_SIZE_CHANGE; i++) {
            CoreInfo* candidate = &core_info[i];
            if (isValid(candidate)) {
                /* Possible big core candidate, in lowest CState */
                if (!core || (candidate->c_state < core->c_state)) {
                    core = candidate;
                }
            }
        }    
    }
    
    /* We found a core to wake up, so update it */
    if (core != NULL) {
        bool transition = (core->c_state >= C3);
        update_core (core, C1, pid, transition);
        if (!transition) {
            LoadContext(core->running, core->idx);
            RunCore(core->idx);
            updatePState(core, P0);
        }
        return true;
    }
    return false;
} 

/* Check if there exists a valid core in the system */
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
    if (!existsIdle() || readyQ.size() < READY_Q_THRESH) {
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
        /* check if we're big core and there's not much work, go to sleep */
        if (readyQ.size() < READY_Q_THRESH && current_core < CORE_SIZE_CHANGE)
            update_core (core, C4, InvalidProcessId(), false); /* Power down large cores quickly */
        else {
            update_core (core, core->c_state, readyQ.front(), false);
            readyQ.pop();

            LoadContext(core_info[current_core].running, current_core);
            RunCore(current_core);
            updatePState(core, P3); 
        }
    } else { /* We are out of work, we can go to sleep */
        update_core (core, C2, InvalidProcessId(), false);
    }
}

/* Takes a core and preempts it */
void preempt (CoreInfo* core) {
    updatePState(core, P3);
    SaveContext(core->running, core->idx);
    readyQ.push(core->running);
    core->running = readyQ.front();
    readyQ.pop();
    LoadContext(core->running, core->idx);
    RunCore(core->idx);
}

/* Checks if a core is preemptable */
bool preemptable (CoreInfo* core) {
    return isRunning(core->idx) && !core->isTransitioning;
}

/* Runs every timer interrupt, updates cores C states and preempts */
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
            update_core (core, c_state, core->running, transition);
        } 

        /* Preempt our cores if not idling */ 
        else if (preemptable (core)) {
            preempt (core);
        }
    }
}

/* Debug print statements for all cores */
void debugPrinting() {
    std::cout << "rQ: " << readyQ.size() << std::endl;
    CoreInfo* core;
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        core = &core_info[i];
        std::cout << "C:" << i << " CS:" << core->c_state << " PS:" << core->p_state << " trans: " << core->isTransitioning << " | ";
    }
    std::cout << std::endl;
}

/* Gets a preemptable big core */
CoreInfo* getRunningLarge() {
    for (int i = 0; i < CORE_SIZE_CHANGE; i++) {
        CoreInfo* core = &core_info[i];
        if (preemptable(core))
            return core;
    }
    return NULL;
}

/* Gets a valid idle small core */
CoreInfo* getIdleSmall() {
    for (int i = CORE_SIZE_CHANGE; i < NUMBER_OF_CORES; i++) {
        CoreInfo* core = &core_info[i];
        if (isValid(core))
            return core;
    }
    return NULL;
}

/* Not used, but kept for reference, functionality moved to exit process */
void load_balancing () {
    if (readyQ.size() < 10) {
        // Check how many cores are running where
        int small_cores = 0;
        int large_cores = 0;
        
        for (int i = 0; i < CORE_SIZE_CHANGE; i++) {
            if (preemptable(&core_info[i])) {
                large_cores++;
            }
        }
        for (int i = CORE_SIZE_CHANGE; i < NUMBER_OF_CORES; i++) {
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
                SaveContext(pid, large->idx);
                update_core(large, C2, InvalidProcessId(), false);
                bool transition = (small->c_state >= C3);
                update_core(small, C1, pid, transition);
            }
        }
    }
}

/* Returns number of cores to run based on ratio of q size */
int enoughCores() {
    if (readyQ.size() == 0)
        return 0;
    int awakeCount = 0;
    for (int i = 0; i < NUMBER_OF_CORES; i++) {
        /* Don't overtake a core that's running, or one that's transitioning to C1 */
        if (isRunning(i) || (core_info[i].isTransitioning && core_info[i].c_state == C1))
            awakeCount++;
    }
    if (awakeCount == 0)
        return 1;
    if ((readyQ.size() / awakeCount) > CORE_RATIO)
        return (readyQ.size() / CORE_RATIO) - awakeCount;
    return 0;
}

/* Helper function to wake up necesary sleeping cores */
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
        // debugPrinting(); // Uncomment to print debug
        wake_cores ();
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
        updatePState(core, P0);
    }
}

void SimulationComplete(Time_t now) {
    // Add any bookkeeping or statistics that you would want to collect. Program terminates after this function returns.
    std::cout << "Run stopped at " << FormatTime(now) << " after consuming " << GetTotalEnergyConsumed()/3600000000.0 << " kWh" << std::endl;
}