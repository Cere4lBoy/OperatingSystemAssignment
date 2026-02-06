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
#include <sstream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <vector>

#include "common.hpp"

constexpr const char* SHM_NAME = "/race_game_shm";

/* ---------- SIGCHLD HANDLER ---------- */
void sigchld_handler(int) {
    while (waitpid(-1, nullptr, WNOHANG) > 0);
}

void* logger_thread(void* arg) {
    SharedData* shared = static_cast<SharedData*>(arg);
    FILE* log = fopen("game.log", "a");

    if (!log) {
        perror("fopen game.log");
        return nullptr;
    }

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

void load_scores(SharedData* shared) {
    pthread_mutex_lock(&shared->score_mutex);

    // default scores
    for (int i = 0; i < MAX_PLAYERS; i++)
        shared->scores[i] = 0;

    FILE* f = fopen("scores.txt", "r");
    if (!f) {
        pthread_mutex_unlock(&shared->score_mutex);
        return; // file doesn't exist yet
    }

    int id, score;
    while (fscanf(f, "Player%d %d", &id, &score) == 2) {
        if (id >= 0 && id < MAX_PLAYERS)
            shared->scores[id] = score;
    }

    fclose(f);
    pthread_mutex_unlock(&shared->score_mutex);
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
}

void reset_game(SharedData* shared) {
    pthread_mutex_lock(&shared->game_mutex);

    std::cout << "[SERVER] Resetting game state...\n";

    for (int i = 0; i < MAX_PLAYERS; i++) {
        shared->game.positions[i] = 0;
    }

    shared->game.current_turn = 0;
    shared->game.winner = -1;
    shared->game.game_over = 0;
    shared->game.game_active = 1;

    pthread_mutex_unlock(&shared->game_mutex);

    // log reset
    pthread_mutex_lock(&shared->log_mutex);
    snprintf(shared->log_buffer, sizeof(shared->log_buffer),
             "Game reset. New game started.");
    shared->log_pending = 1;
    pthread_mutex_unlock(&shared->log_mutex);
}

void print_leaderboard(int positions[], int num_players)
{
    std::vector<std::pair<int, int>> leaderboard;
    for (int i = 0; i < num_players;++i)
    {
        leaderboard.emplace_back(i, positions[i]);
    }

    std::sort(leaderboard.begin(), leaderboard.end(), [](const auto& a, const auto& b)
    {
       return a.second > b.second;
    });

    std::cout << "=================================================\n";
    std::cout << " LEADERBOARD:\n";

    const char* suffixes[] = {"1st", "2nd", "3rd"};
    for (size_t rank = 0; rank < leaderboard.size(); ++rank)
    {
        int player_id = leaderboard[rank].first;
        int distance = leaderboard[rank].second;
        std::cout << " " << suffixes[rank] << ": PLAYER " << (player_id + 1)
                  << " (Distance: " << distance << ")\n";
    }
    std::cout << "==================================================\n";
}

std::string generate_race_track(int positions[], int goal=WIN_POSITION)
{
    std::ostringstream ss;

    ss << "===============================================\n";
    ss << " RACE TRACK (Goal: " << goal << "m)\n";
    ss << "===============================================\n\n";

    for (int p = 0; p < MAX_PLAYERS; ++p)
    {
        int pos = positions[p];
        if (pos > goal) pos = goal;

        ss << "P" << (p + 1) << " [" << std::setw(2) << std::setfill('0') << pos << "m]";
        ss << "|";
        for (int i = 0; i < pos; ++i) ss << "-";
        ss << " -O- ";
        for (int i = pos + 1; i < goal; ++i) ss << "-";
        ss << "|\n";
        ss << "        |";
        for (int i = 0; i < pos; ++i) ss << " ";
        ss << " | # | ";
        for (int i = pos + 1; i < goal; ++i) ss << " ";
        ss << "|\n";
        ss << "        |";
        for (int i = 0; i < pos; ++i) ss << "-";
        ss << " -O- ";
        for (int i = pos + 1; i < goal; ++i) ss << "-";
        ss << "|\n\n";
    }
    return ss.str();
}

int main() {
    srand(time(nullptr));

    /* ---------- SIGNAL HANDLER ---------- */
    struct sigaction sa{};
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);

    /* ---------- SHARED MEMORY ---------- */
    shm_unlink(SHM_NAME);
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(SharedData));

    SharedData* shared = static_cast<SharedData*>(
        mmap(nullptr, sizeof(SharedData),
             PROT_READ | PROT_WRITE,
             MAP_SHARED, shm_fd, 0)
    );

    std::memset(shared, 0, sizeof(SharedData));

    /* ---------- MUTEX INIT ---------- */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shared->game_mutex, &attr);
    pthread_mutex_init(&shared->log_mutex, &attr);
    pthread_mutex_init(&shared->score_mutex, &attr);

    load_scores(shared);

    shared->game.current_turn = 0;
    shared->game.game_active = 0;
    shared->game.winner = -1;
    shared->game.game_over = 0;

    /* ---------- LOGGER THREAD ---------- */
    shared->log_pending = 0;

    pthread_t logger;
    pthread_create(&logger, nullptr, logger_thread, shared);

    /* ---------- FIFOS ---------- */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        std::string in_fifo  = "/tmp/player_" + std::to_string(i) + "_in";
        std::string out_fifo = "/tmp/player_" + std::to_string(i) + "_out";
        unlink(in_fifo.c_str());
        unlink(out_fifo.c_str());
        mkfifo(in_fifo.c_str(), 0666);
        mkfifo(out_fifo.c_str(), 0666);
    }

    std::cout << "[SERVER] Waiting for 3 players...\n";

    /* ---------- FORK 3 PLAYERS ---------- */
    for (int player_id = 0; player_id < MAX_PLAYERS; player_id++) {
        pid_t pid = fork();

        if (pid == 0) {
            /* ===== CHILD PROCESS ===== */

            std::string in_fifo  = "/tmp/player_" + std::to_string(player_id) + "_in";
            std::string out_fifo = "/tmp/player_" + std::to_string(player_id) + "_out";

            int fd_in  = open(in_fifo.c_str(), O_RDWR);
            int fd_out = open(out_fifo.c_str(), O_RDWR);

            pthread_mutex_lock(&shared->game_mutex);
            shared->players[player_id].connected = 1;
            shared->game.active_players++;

            if (shared->game.active_players == 3) {
                shared->game.game_active = 1;
                shared->game.current_turn = 0;
                pthread_mutex_lock(&shared->log_mutex);
                snprintf(shared->log_buffer, sizeof(shared->log_buffer),
                        "Game started with %d players", MAX_PLAYERS);
                shared->log_pending = 1;
                pthread_mutex_unlock(&shared->log_mutex);

                std::cout << "[SERVER] Game started!\n";

            }

            pthread_mutex_unlock(&shared->game_mutex);

            char buffer[64];

            while (true) {
                pthread_mutex_lock(&shared->game_mutex);

                if (shared->game.game_over) {
                    pthread_mutex_unlock(&shared->game_mutex);
                    sleep(2);                 // short pause between games
                    reset_game(shared);
                    continue;
                }

                if (!shared->game.game_active ||
                    shared->game.current_turn != player_id) {
                    pthread_mutex_unlock(&shared->game_mutex);
                    usleep(100000);
                    continue;
                }

                write(fd_out, "YOUR_TURN\n", 10);
                pthread_mutex_unlock(&shared->game_mutex);

                std::memset(buffer, 0, sizeof(buffer));
                read(fd_in, buffer, sizeof(buffer));

                int dice = (rand() % 6) + 1;

                pthread_mutex_lock(&shared->game_mutex);
                if(!shared->game.game_over) {
                    shared->game.positions[player_id] += dice;
                    print_leaderboard(shared->game.positions, MAX_PLAYERS); }

                pthread_mutex_unlock(&shared->game_mutex);

                std::string display = generate_race_track(shared->game.positions);
                for (int i = 0; i < MAX_PLAYERS; i++)
                {
                    std::string out_fifo = "/tmp/player_" + std::to_string(i) + "_out";
                    int fd = open(out_fifo.c_str(), O_WRONLY | O_NONBLOCK);
                    if (fd >= 0)
                    {
                        write(fd, display.c_str(), display.size());
                        close(fd);
                    }
                }
                pthread_mutex_unlock(&shared->game_mutex);

                std::cout << "[SERVER] Player " << player_id
                          << " rolled " << dice
                          << " (pos=" << shared->game.positions[player_id] << ")\n";

                pthread_mutex_lock(&shared->log_mutex);
                snprintf(shared->log_buffer, sizeof(shared->log_buffer),
                         "Player %d rolled %d (position=%d)",
                         player_id, dice, shared->game.positions[player_id]);
                shared->log_pending = 1;
                pthread_mutex_unlock(&shared->log_mutex);

                if (shared->game.positions[player_id] >= WIN_POSITION) {
                    shared->game.winner = player_id;

                    // update score
                    pthread_mutex_lock(&shared->score_mutex);
                    shared->scores[player_id]++;
                    pthread_mutex_unlock(&shared->score_mutex);

                    // log score update
                    pthread_mutex_lock(&shared->log_mutex);
                    snprintf(shared->log_buffer, sizeof(shared->log_buffer),
                             "Player %d WON. Total wins = %d",
                             player_id, shared->scores[player_id]);
                    shared->log_pending = 1;
                    pthread_mutex_unlock(&shared->log_mutex);

                    save_scores(shared);

                    write(fd_out, "YOU_WIN\n", 8);
                    std::cout << "[SERVER] Player " << player_id << " wins!\n";

                    shared->game.game_active = 0;
                    shared->game.game_over = 1;

                    pthread_mutex_unlock(&shared->game_mutex);
                }

                shared->game.current_turn =
                    (shared->game.current_turn + 1) % MAX_PLAYERS;

                pthread_mutex_unlock(&shared->game_mutex);
            }
        }
    }

    while (true) pause();
}
