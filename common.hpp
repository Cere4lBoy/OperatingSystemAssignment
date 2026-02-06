#ifndef COMMON_HPP
#define COMMON_HPP

#include <pthread.h>
#include <string>

constexpr int MAX_PLAYERS = 3;     // 🔴 CHANGED FROM 5 → 3
constexpr int MAX_NAME_LEN = 32;
constexpr int WIN_POSITION = 40;

// ---- Game State ----
struct GameState {
    int positions[MAX_PLAYERS];
    int current_turn;
    int active_players;
    int game_active;
    int winner;
    int game_over;   // 0 = running, 1 = ended
};

// ---- Player Info ----
struct Player {
    int connected;
    char name[MAX_NAME_LEN];
};

// ---- Shared Memory ----
struct SharedData {
    GameState game;
    Player players[MAX_PLAYERS];

    pthread_mutex_t game_mutex;
    pthread_mutex_t log_mutex;
    pthread_mutex_t score_mutex;

    // logger
    char log_buffer[256];
    int log_pending;

    // scores
    int scores[MAX_PLAYERS];
};


// ---- Logger ----
char log_buffer[256];
int log_pending;   // 0 = none, 1 = has log

#endif
