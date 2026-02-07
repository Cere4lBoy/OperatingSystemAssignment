================================================================================
CSN6214 OPERATING SYSTEMS ASSIGNMENT
Concurrent Networked Board Game Interpreter - Dice Race Game
================================================================================

TEAM INFORMATION:
----------------
Tutorial/Lab Section: [TT1L]

Member 1: [Iman Thaqif bin Nasaruddin] - [242UC245G9]
Member 2: [Muhammad Syazrin Muhaimin bin Zaiful Azrai] - [242UC244PQ]
Member 3: [Yang Alya Mysarah binti Yang Zamzari Habali] - [242UC245GA]


================================================================================
GAME DESCRIPTION:
================================================================================

This is a multiplayer dice race game for 3-5 players. Players take turns
rolling a virtual 6-sided die and advancing their position on a race track.
The first player to reach position 20 or beyond wins the game.

The game demonstrates advanced operating systems concepts including:
- Hybrid concurrency (multiprocessing + multithreading)
- Inter-process communication using POSIX shared memory and named FIFOs
- Process-shared synchronization primitives
- Concurrent logging with a dedicated thread
- Round Robin scheduling with a dedicated thread
- Persistent score tracking across multiple games


================================================================================
COMPILATION INSTRUCTIONS:
================================================================================

PREREQUISITES:
--------------
- Linux operating system (tested on Ubuntu 24.04)
- GCC/G++ compiler with C++17 support
- POSIX threads library (pthread)
- POSIX shared memory support

COMPILE THE PROJECT:
--------------------
Navigate to the project directory and run:

    make

This will compile both the server and client executables.

EXPECTED OUTPUT:
----------------
    g++ -Wall -pthread -std=c++17 server.cpp -o server
    g++ -Wall -pthread -std=c++17 client.cpp -o client

Two executable files will be created:
    - server (game server)
    - client (player client)

CLEAN BUILD ARTIFACTS:
----------------------
To remove compiled binaries:

    make clean


================================================================================
RUNNING THE GAME:
================================================================================

STEP 1: START THE SERVER
-------------------------
In your first terminal, run:

    ./server

You will be prompted:
    Enter number of players (3-5):

Type a number between 3 and 5 and press ENTER.

The server will display:
    [SERVER] Logger thread started
    [SERVER] Scheduler thread started
    [SERVER] Waiting for X players...


STEP 2: START THE CLIENTS
--------------------------
Open separate terminal windows for each player (3-5 terminals depending on
the number of players you specified).

In each terminal, run:

    ./client

You will be prompted:
    Enter player ID (0-4 for up to 5 players):

Enter a unique ID for each player:
    - Terminal 2: Enter 0
    - Terminal 3: Enter 1
    - Terminal 4: Enter 2
    - Terminal 5: Enter 3 (if 4-5 players)
    - Terminal 6: Enter 4 (if 5 players)

Each client will display:
    ✅ Connected as Player X
    Waiting for game to start...


STEP 3: PLAY THE GAME
---------------------
Once all players have connected, the game will automatically start.

The server will announce:
    [SERVER] All X players connected. Game started!

Players will take turns in Round Robin order (Player 0, 1, 2, ...).

When it's your turn, you will see:
    🎲 Your turn! Press ENTER to roll dice...

Press ENTER to roll the dice. The server will:
- Generate a random dice value (1-6)
- Advance your position
- Log the move
- Advance to the next player's turn

When a player wins (reaches position 20 or beyond):
    🎉🎉🎉 YOU WIN! 🎉🎉🎉

The game will pause for 3 seconds, then automatically reset for a new game.


STEP 4: SHUTDOWN THE SERVER
----------------------------
To gracefully shut down the server and save all scores:

Press Ctrl+C in the server terminal.

The server will display:
    [SERVER] Received SIGINT. Shutting down gracefully...
    [SERVER] Scores saved to scores.txt
    [SERVER] Cleanup complete. Goodbye!


================================================================================
GAME RULES:
================================================================================

NUMBER OF PLAYERS:
------------------
- Minimum: 3 players
- Maximum: 5 players
- All players must connect before the game starts

GAMEPLAY:
---------
1. Players take turns in Round Robin order (0 → 1 → 2 → ... → 0)
2. On each turn, the active player rolls a 6-sided die
3. The player advances forward by the rolled amount
4. Dice rolling is server-side (ensures fairness and prevents cheating)
5. All game state is shared across processes via POSIX shared memory

WIN CONDITION:
--------------
- First player to reach position 20 or beyond WINS
- Winner's score is incremented by 1
- Scores are saved to scores.txt

MULTI-GAME SUPPORT:
-------------------
- After a game ends, the server automatically resets for a new game
- Player positions reset to 0
- Scores persist across games
- No need to restart the server or reconnect clients

DISCONNECTION HANDLING:
-----------------------
- If a player disconnects, the scheduler skips their turn
- Game continues with remaining connected players


================================================================================
DEPLOYMENT MODE:
================================================================================

MODE: Single-machine mode

COMMUNICATION MECHANISM:
------------------------
- Client-Server Communication: Named POSIX FIFOs (named pipes)
  
  For each player ID (0-4), two FIFOs are created:
    - Server → Client: /tmp/player_X_out (server writes, client reads)
    - Client → Server: /tmp/player_X_in  (client writes, server reads)

- Internal Server Communication: POSIX shared memory segment
  
  Shared memory segment: /race_game_shm
  Contains: Game state, player info, scores, mutexes, logger buffer

ARCHITECTURE:
-------------
- Parent Process: Runs main server loop, logger thread, scheduler thread
- Child Processes: One forked process per connected player (3-5 processes)
- Threads: 2 POSIX threads (logger + scheduler) in parent process


================================================================================
SYSTEM ARCHITECTURE:
================================================================================

HYBRID CONCURRENCY MODEL:
--------------------------
This project uses BOTH multiprocessing AND multithreading:

1. MULTIPROCESSING (fork):
   - Server forks a child process for each connected player
   - Each child process handles one player's game session
   - Provides isolation between players
   - Zombie processes are reaped using SIGCHLD signal handler

2. MULTITHREADING (pthreads):
   - Logger Thread: Writes game events to game.log concurrently
   - Scheduler Thread: Manages Round Robin turn order
   - Both threads run in the parent server process

SYNCHRONIZATION:
----------------
- Process-shared mutexes (PTHREAD_PROCESS_SHARED attribute)
  - game_mutex: Protects game state (positions, turn, winner)
  - log_mutex: Protects concurrent logging operations
  - score_mutex: Protects score updates and file I/O

- All shared memory accesses are protected by mutexes
- Prevents race conditions across processes and threads


================================================================================
PERSISTENT SCORING:
================================================================================

SCORES FILE: scores.txt

FILE FORMAT:
------------
Each line contains:
    PlayerX Y

Where:
    X = Player ID (0-4)
    Y = Total number of wins

Example:
    Player0 3
    Player1 1
    Player2 5
    Player3 0
    Player4 2

LOADING:
--------
- Scores are loaded from scores.txt when the server starts
- If scores.txt doesn't exist, all scores default to 0
- Scores are stored in shared memory for fast access

UPDATING:
---------
- When a player wins, their score is atomically incremented
- Score update is protected by score_mutex
- Scores are immediately saved to scores.txt after each game

SAVING:
-------
- Scores are saved to scores.txt:
  1. After each game ends (automatic)
  2. When server shuts down via Ctrl+C (SIGINT handler)


================================================================================
LOGGING:
================================================================================

LOG FILE: game.log

The concurrent logger thread writes all game events to game.log in real-time.

LOGGED EVENTS:
--------------
- Game start with number of players
- Each player's dice roll and new position
- Turn changes managed by scheduler
- Game wins with final scores
- Game resets for new games

VIEWING LOGS:
-------------
To watch logs in real-time while playing:

    tail -f game.log

EXAMPLE LOG OUTPUT:
-------------------
    ========== GAME STARTED: 3 PLAYERS ==========
    Player 0 rolled 4 (position=4)
    [SCHEDULER] Turn advanced to Player 1
    Player 1 rolled 6 (position=6)
    [SCHEDULER] Turn advanced to Player 2
    Player 2 rolled 3 (position=3)
    [SCHEDULER] Turn advanced to Player 0
    Player 0 rolled 5 (position=9)
    ...
    Player 2 WON! Total wins = 1
    ========== NEW GAME STARTED ==========


================================================================================
FILES IN THIS PROJECT:
================================================================================

SOURCE FILES:
-------------
server.cpp      - Main game server implementation
                  • Creates shared memory and FIFOs
                  • Forks child processes for each player
                  • Creates logger and scheduler threads
                  • Handles SIGCHLD and SIGINT signals

client.cpp      - Player client implementation
                  • Connects to server via FIFOs
                  • Waits for turn notifications
                  • Sends roll commands to server
                  • Displays game status

common.hpp      - Shared data structures and constants
                  • SharedData struct (game state, mutexes, scores)
                  • GameState struct (positions, turn, winner)
                  • Player struct (connection status)
                  • Constants (MAX_PLAYERS, WIN_POSITION)

Makefile        - Build configuration
                  • Compiler: g++
                  • Flags: -Wall -pthread -std=c++17
                  • Targets: all, server, client, clean

GENERATED FILES:
----------------
server          - Compiled server executable
client          - Compiled client executable
game.log        - Game event log (created at runtime)
scores.txt      - Persistent player scores (created at runtime)

TEMPORARY FILES (created at runtime):
--------------------------------------
/dev/shm/race_game_shm   - POSIX shared memory segment
/tmp/player_0_in         - FIFO: Client 0 → Server
/tmp/player_0_out        - FIFO: Server → Client 0
/tmp/player_1_in         - FIFO: Client 1 → Server
/tmp/player_1_out        - FIFO: Server → Client 1
... (up to player_4_in/out for 5 players)


================================================================================
TROUBLESHOOTING:
================================================================================

PROBLEM: "Cannot create FIFO" or "Permission denied"
SOLUTION: 
    sudo chmod 666 /tmp/player_*
    Or run server with appropriate permissions

PROBLEM: "Shared memory segment already exists"
SOLUTION:
    rm /dev/shm/race_game_shm
    Then restart the server

PROBLEM: "Client cannot connect"
SOLUTION:
    - Make sure server is running first
    - Ensure player ID is between 0-4
    - Check that you haven't exceeded the number of players set on server

PROBLEM: Zombie processes accumulate
SOLUTION:
    - Verify SIGCHLD handler is working
    - Check `ps aux | grep server` for zombie processes
    - If needed: pkill -9 server and restart

PROBLEM: Server doesn't shut down cleanly
SOLUTION:
    - Press Ctrl+C to trigger SIGINT handler
    - If stuck, use: pkill -9 server
    - Manually clean up: rm /dev/shm/race_game_shm /tmp/player_*

PROBLEM: Compilation error about -lrt
SOLUTION:
    On some older Linux systems, add -lrt to CXXFLAGS in Makefile:
    CXXFLAGS=-Wall -pthread -std=c++17 -lrt


================================================================================
EXAMPLE GAMEPLAY SESSION:
================================================================================

TERMINAL 1 (Server):
--------------------
$ ./server
Enter number of players (3-5): 3
[SERVER] Logger thread started
[SERVER] Scheduler thread started
[SERVER] No existing scores.txt, starting fresh
[SERVER] Waiting for 3 players...
[SERVER] All 3 players connected. Game started!
[SERVER] Player 0 rolled 4 → position 4
[SCHEDULER] Turn -> Player 1
[SERVER] Player 1 rolled 6 → position 6
[SCHEDULER] Turn -> Player 2
[SERVER] Player 2 rolled 3 → position 3
...
[SERVER] 🎉 Player 1 WINS!
[SERVER] Scores saved to scores.txt
[SERVER] Resetting game state for new game...
^C
[SERVER] Received SIGINT. Shutting down gracefully...
[SERVER] Scores saved to scores.txt
[SERVER] Cleanup complete. Goodbye!

TERMINAL 2 (Client - Player 0):
--------------------------------
$ ./client
Enter player ID (0-4 for up to 5 players): 0
✅ Connected as Player 0
Waiting for game to start...

🎲 Your turn! Press ENTER to roll dice...
[Press ENTER]

🎲 Your turn! Press ENTER to roll dice...
[Press ENTER]
...

TERMINAL 3 (Client - Player 1):
--------------------------------
$ ./client
Enter player ID (0-4 for up to 5 players): 1
✅ Connected as Player 1
Waiting for game to start...

🎲 Your turn! Press ENTER to roll dice...
[Press ENTER]
...
🎉🎉🎉 YOU WIN! 🎉🎉🎉
Waiting for next game...

🎲 Your turn! Press ENTER to roll dice...
[New game starts automatically]


================================================================================
TECHNICAL HIGHLIGHTS:
================================================================================

✓ Hybrid Concurrency Model
  - Combines fork() for process isolation
  - Uses pthreads for concurrent internal tasks
  
✓ Process-Shared Synchronization
  - All mutexes initialized with PTHREAD_PROCESS_SHARED
  - Safe coordination between forked children and parent threads
  
✓ Round Robin Scheduler
  - Dedicated thread manages turn order
  - Automatically skips disconnected players
  - Updates turn state in shared memory
  
✓ Concurrent Logger
  - Dedicated thread writes to game.log
  - Non-blocking to gameplay
  - Thread-safe queue using mutex + flag
  
✓ Persistent Scoring
  - Scores loaded at startup
  - Atomic updates protected by mutex
  - Saved after each game and on shutdown
  
✓ Multi-Game Support
  - Server automatically resets game state
  - No need to restart or reconnect
  - Scores persist across sessions

✓ Zombie Prevention
  - SIGCHLD signal handler
  - Non-blocking waitpid() with WNOHANG
  
✓ Graceful Shutdown
  - SIGINT handler saves scores before exit
  - Cleans up shared memory and FIFOs


================================================================================
SYSTEM REQUIREMENTS:
================================================================================

OPERATING SYSTEM:
-----------------
- Linux (tested on Ubuntu 24.04)
- Kernel with POSIX shared memory support
- /dev/shm/ directory available

COMPILER:
---------
- GCC/G++ version 7.0 or higher
- C++17 standard support
- POSIX threads library

LIBRARIES:
----------
- pthread (POSIX threads)
- rt (Real-time extensions, may be needed on older systems)

PERMISSIONS:
------------
- Read/write access to /tmp/ directory (for FIFOs)
- Read/write access to /dev/shm/ (for shared memory)
- Execute permissions on compiled binaries
