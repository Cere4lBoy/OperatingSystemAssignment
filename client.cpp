#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>

int main() {
    int player_id;
    std::cout << "Enter player ID (0-4 for up to 5 players): ";
    std::cin >> player_id;
    
    if (player_id < 0 || player_id > 4) {
        std::cerr << "Invalid player ID (must be 0-4)\n";
        return 1;
    }

    std::string in_fifo  = "/tmp/player_" + std::to_string(player_id) + "_out";
    std::string out_fifo = "/tmp/player_" + std::to_string(player_id) + "_in";

    int fd_in  = open(in_fifo.c_str(), O_RDONLY);
    int fd_out = open(out_fifo.c_str(), O_WRONLY);

    if (fd_in < 0 || fd_out < 0) {
        perror("FIFO open");
        return 1;
    }

    std::cout << "✅ Connected as Player " << player_id << "\n";
    std::cout << "Waiting for game to start...\n";

    char buffer[64];

    while (true) {
        ssize_t bytes = read(fd_in, buffer, sizeof(buffer));
        if (bytes <= 0) break;

        if (std::strncmp(buffer, "YOUR_TURN", 9) == 0) {
            std::cout << "\n🎲 Your turn! Press ENTER to roll dice...";
            std::cin.ignore();
            std::cin.get();
            write(fd_out, "ROLL\n", 5);
        }
        else if (std::strncmp(buffer, "YOU_WIN", 7) == 0) {
            std::cout << "\n🎉🎉🎉 YOU WIN! 🎉🎉🎉\n";
            std::cout << "Waiting for next game...\n";
            continue;
        }
        else if (std::strncmp(buffer, "GAME_OVER", 9) == 0) {
            std::cout << "Game over. Waiting for next game...\n";
        }
    }

    close(fd_in);
    close(fd_out);
    return 0;
}

