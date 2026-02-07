#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <csignal>
#include <sys/wait.h>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <ctime>

#include "common.hpp"

constexpr const char* SHM_NAME = "/race_game_shm";

// ========================================
// Global pointer for signal handler
// ========================================
SharedData* g_shared = nullptr;

/* ========================================
   SIGNAL HANDLER - Reap Zombie Processes
   ======================================== */
void sigchld_handler(int) {
    while (waitpid(-1, nullptr, WNOHANG) > 0);
}

// ========================================
// SIGINT Handler for graceful shutdown
// ========================================
void sigint_handler(int) {
    std::cout << "\n[SERVER] Received SIGINT. Shutting down gracefully...\n";
    
    if (g_shared) {
        save_scores(g_shared);
        
        // Cleanup shared memory
        munmap(g_shared, sizeof(SharedData));
        shm_unlink(SHM_NAME);
        
        std::cout << "[SERVER] Cleanup complete. Goodbye!\n";
    }
    
    exit(0);
}

/* ========================================
   LOGGER THREAD - Concurrent Log Writing
   ======================================== */
void* logger_thread(void* arg) {
    SharedData* shared = static_cast<SharedData*>(arg);
    FILE* log = fopen("game.log", "a");

    if (!log) {
        perror("fopen game.log");
        return nullptr;
    }

    std::cout << "[SERVER] Logger thread started\n";

    while (true) {
        pthread_mutex_lock(&shared->log_mutex);

        if (shared->log_pending) {
            fprintf(log, "%s\n", shared->log_buffer);
            fflush(log);
            shared->log_pending = 0;
        }

        pthread_mutex_unlock(&shared->log_mutex);
        usleep(100000); // 100ms
    }

    fclose(log);
    return nullptr;
}

/* ========================================
   SCHEDULER THREAD - Round Robin Turn Management
   ======================================== */
void* scheduler_thread(void* arg) {
    SharedData* shared = static_cast<SharedData*>(arg);
    
    std::cout << "[SERVER] Scheduler thread started\n";

    while (true) {
        pthread_mutex_lock(&shared->game_mutex);

        // Only manage turns when game is active
        if (shared->game.game_active && !shared->game.game_over && shared->game.turn_complete) {
            
            // Advance to next player (Round Robin)
            int next_turn = (shared->game.current_turn + 1) % shared->game.num_players;
            
            // Skip disconnected players
            int attempts = 0;
            while (!shared->players[next_turn].connected && attempts < shared->game.num_players) {
                next_turn = (next_turn + 1) % shared->game.num_players;
                attempts++;
            }

            // Update turn
            shared->game.current_turn = next_turn;
            shared->game.turn_complete = 0;  // Reset flag for next turn

            // Log turn change
            pthread_mutex_lock(&shared->log_mutex);
            snprintf(shared->log_buffer, sizeof(shared->log_buffer),
                     "[SCHEDULER] Turn advanced to Player %d", next_turn);
            shared->log_pending = 1;
            pthread_mutex_unlock(&shared->log_mutex);

            std::cout << "[SCHEDULER] Turn -> Player " << next_turn << "\n";
        }

        pthread_mutex_unlock(&shared->game_mutex);
        usleep(50000); // Check every 50ms
    }

    return nullptr;
}

/* ========================================
   PERSISTENT SCORING
   ======================================== */
void load_scores(SharedData* shared) {
    pthread_mutex_lock(&shared->score_mutex);

    // Initialize default scores
    for (int i = 0; i < MAX_PLAYERS; i++)
        shared->scores[i] = 0;

    FILE* f = fopen("scores.txt", "r");
    if (!f) {
        pthread_mutex_unlock(&shared->score_mutex);
        std::cout << "[SERVER] No existing scores.txt, starting fresh\n";
        return;
    }

    int id, score;
    while (fscanf(f, "Player%d %d", &id, &score) == 2) {
        if (id >= 0 && id < MAX_PLAYERS)
            shared->scores[id] = score;
    }

    fclose(f);
    pthread_mutex_unlock(&shared->score_mutex);
    std::cout << "[SERVER] Scores loaded from scores.txt\n";
}

void save_scores(SharedData* shared) {
    pthread_mutex_lock(&shared->score_mutex);

    FILE* f = fopen("scores.txt", "w");
    if (!f) {
        perror("scores.txt");
        pthread_mutex_unlock(&shared->score_mutex);
        return;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        fprintf(f, "Player%d %d\n", i, shared->scores[i]);
    }

    fclose(f);
    pthread_mutex_unlock(&shared->score_mutex);
    std::cout << "[SERVER] Scores saved to scores.txt\n";
}

/* ========================================
   GAME RESET - Multi-Game Support
   ======================================== */
void reset_game(SharedData* shared) {
    pthread_mutex_lock(&shared->game_mutex);

    std::cout << "[SERVER] Resetting game state for new game...\n";

    for (int i = 0; i < MAX_PLAYERS; i++) {
        shared->game.positions[i] = 0;
    }

    shared->game.current_turn = 0;
    shared->game.winner = -1;
    shared->game.game_over = 0;
    shared->game.game_active = 1;
    shared->game.turn_complete = 0;

    pthread_mutex_unlock(&shared->game_mutex);

    // Log reset
    pthread_mutex_lock(&shared->log_mutex);
    snprintf(shared->log_buffer, sizeof(shared->log_buffer),
             "========== NEW GAME STARTED ==========");
    shared->log_pending = 1;
    pthread_mutex_unlock(&shared->log_mutex);
}

/* ========================================
   MAIN SERVER
   ======================================== */
int main() {
    srand(time(nullptr));

    /* ---------- GET NUMBER OF PLAYERS ---------- */
    int num_players;
    std::cout << "Enter number of players (3-5): ";
    std::cin >> num_players;
    
    if (num_players < 3 || num_players > 5) {
        std::cerr << "Error: Must be 3-5 players\n";
        return 1;
    }

    /* ---------- SIGNAL HANDLERS ---------- */
    struct sigaction sa{};
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);

    // ========================================
    // Register SIGINT handler
    // ========================================
    signal(SIGINT, sigint_handler);

    /* ---------- SHARED MEMORY ---------- */
    shm_unlink(SHM_NAME);
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return 1;
    }
    
    ftruncate(shm_fd, sizeof(SharedData));

    SharedData* shared = static_cast<SharedData*>(
        mmap(nullptr, sizeof(SharedData),
             PROT_READ | PROT_WRITE,
             MAP_SHARED, shm_fd, 0)
    );

    if (shared == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    std::memset(shared, 0, sizeof(SharedData));

    // ========================================
    // Set global pointer for signal handler
    // ========================================
    g_shared = shared;

    /* ---------- PROCESS-SHARED MUTEX INIT ---------- */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shared->game_mutex, &attr);
    pthread_mutex_init(&shared->log_mutex, &attr);
    pthread_mutex_init(&shared->score_mutex, &attr);

    pthread_mutexattr_destroy(&attr);

    /* ---------- INITIALIZE GAME STATE ---------- */
    load_scores(shared);

    shared->game.num_players = num_players;
    shared->game.current_turn = 0;
    shared->game.game_active = 0;
    shared->game.winner = -1;
    shared->game.game_over = 0;
    shared->game.active_players = 0;
    shared->game.turn_complete = 0;

    /* ---------- LOGGER THREAD ---------- */
    shared->log_pending = 0;

    pthread_t logger;
    if (pthread_create(&logger, nullptr, logger_thread, shared) != 0) {
        perror("pthread_create logger");
        return 1;
    }

    /* ---------- SCHEDULER THREAD ---------- */
    pthread_t scheduler;
    if (pthread_create(&scheduler, nullptr, scheduler_thread, shared) != 0) {
        perror("pthread_create scheduler");
        return 1;
    }

    /* ---------- CREATE FIFOS ---------- */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        std::string in_fifo  = "/tmp/player_" + std::to_string(i) + "_in";
        std::string out_fifo = "/tmp/player_" + std::to_string(i) + "_out";
        unlink(in_fifo.c_str());
        unlink(out_fifo.c_str());
        mkfifo(in_fifo.c_str(), 0666);
        mkfifo(out_fifo.c_str(), 0666);
    }

    std::cout << "[SERVER] Waiting for " << num_players << " players...\n";

    /* ---------- FORK PLAYER PROCESSES ---------- */
    for (int player_id = 0; player_id < num_players; player_id++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            /* ===== CHILD PROCESS (Player Handler) ===== */

            std::string in_fifo  = "/tmp/player_" + std::to_string(player_id) + "_in";
            std::string out_fifo = "/tmp/player_" + std::to_string(player_id) + "_out";

            int fd_in  = open(in_fifo.c_str(), O_RDWR);
            int fd_out = open(out_fifo.c_str(), O_RDWR);

            if (fd_in < 0 || fd_out < 0) {
                perror("FIFO open in child");
                exit(1);
            }

            // Mark player as connected
            pthread_mutex_lock(&shared->game_mutex);
            shared->players[player_id].connected = 1;
            shared->game.active_players++;

            // Start game when all players connected
            if (shared->game.active_players == num_players) {
                shared->game.game_active = 1;
                shared->game.current_turn = 0;
                shared->game.turn_complete = 0;
                
                pthread_mutex_lock(&shared->log_mutex);
                snprintf(shared->log_buffer, sizeof(shared->log_buffer),
                        "========== GAME STARTED: %d PLAYERS ==========", num_players);
                shared->log_pending = 1;
                pthread_mutex_unlock(&shared->log_mutex);

                std::cout << "[SERVER] All " << num_players << " players connected. Game started!\n";
            }

            pthread_mutex_unlock(&shared->game_mutex);

            char buffer[64];

            // Player event loop
            while (true) {
                pthread_mutex_lock(&shared->game_mutex);

                // Handle game over
                if (shared->game.game_over) {
                    pthread_mutex_unlock(&shared->game_mutex);
                    sleep(3); // Pause between games
                    reset_game(shared);
                    continue;
                }

                // Wait for my turn
                if (!shared->game.game_active ||
                    shared->game.current_turn != player_id ||
                    shared->game.turn_complete) {
                    pthread_mutex_unlock(&shared->game_mutex);
                    usleep(100000);
                    continue;
                }

                // It's my turn!
                write(fd_out, "YOUR_TURN\n", 10);
                pthread_mutex_unlock(&shared->game_mutex);

                // Wait for player action
                std::memset(buffer, 0, sizeof(buffer));
                ssize_t bytes = read(fd_in, buffer, sizeof(buffer));
                if (bytes <= 0) {
                    // Player disconnected
                    pthread_mutex_lock(&shared->game_mutex);
                    shared->players[player_id].connected = 0;
                    pthread_mutex_unlock(&shared->game_mutex);
                    break;
                }

                // Roll dice (server-side randomness)
                int dice = (rand() % 6) + 1;

                pthread_mutex_lock(&shared->game_mutex);
                shared->game.positions[player_id] += dice;

                std::cout << "[SERVER] Player " << player_id
                          << " rolled " << dice
                          << " -> position " << shared->game.positions[player_id] << "\n";

                pthread_mutex_lock(&shared->log_mutex);
                snprintf(shared->log_buffer, sizeof(shared->log_buffer),
                         "Player %d rolled %d (position=%d)",
                         player_id, dice, shared->game.positions[player_id]);
                shared->log_pending = 1;
                pthread_mutex_unlock(&shared->log_mutex);

                // Check win condition
                if (shared->game.positions[player_id] >= WIN_POSITION) {
                    shared->game.winner = player_id;

                    // Update score atomically
                    pthread_mutex_lock(&shared->score_mutex);
                    shared->scores[player_id]++;
                    pthread_mutex_unlock(&shared->score_mutex);

                    // Log win
                    pthread_mutex_lock(&shared->log_mutex);
                    snprintf(shared->log_buffer, sizeof(shared->log_buffer),
                             "Player %d WON! Total wins = %d",
                             player_id, shared->scores[player_id]);
                    shared->log_pending = 1;
                    pthread_mutex_unlock(&shared->log_mutex);

                    save_scores(shared);

                    write(fd_out, "YOU_WIN\n", 8);
                    std::cout << "[SERVER] Player " << player_id << " WINS!\n";

                    shared->game.game_active = 0;
                    shared->game.game_over = 1;
                }

                // Signal turn complete (scheduler will advance turn)
                shared->game.turn_complete = 1;

                pthread_mutex_unlock(&shared->game_mutex);
            }

            close(fd_in);
            close(fd_out);
            exit(0);
        }
    }

    /* ---------- PARENT WAITS ---------- */
    std::cout << "[SERVER] All player processes forked. Running...\n";
    std::cout << "[SERVER] Press Ctrl+C to shutdown gracefully\n";
    while (true) pause();

    return 0;
}
