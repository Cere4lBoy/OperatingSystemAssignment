#ifndef COMMON_HPP
#define COMMON_HPP

#include <pthread.h>

constexpr int MAX_PLAYERS = 5;     
constexpr int MAX_NAME_LEN = 32;
constexpr int WIN_POSITION = 20;

// ---- Game State ----
struct GameState {
    int positions[MAX_PLAYERS];
    int current_turn;
    int active_players;
    int num_players;        
    int game_active;
    int winner;
    int game_over;          
    int turn_complete;      
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
    
    // Mutexes
    pthread_mutex_t game_mutex;
    pthread_mutex_t log_mutex;
    pthread_mutex_t score_mutex;
    
    // Logger
    char log_buffer[256];
    int log_pending;        
    
    // Scores
    int scores[MAX_PLAYERS];
};

#endif

