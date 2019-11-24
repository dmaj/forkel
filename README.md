# forkel

Please note: This program has been written in a very short time and not yet as I would like it to be. But at least it runs stable.
This program supports the handling of signals in containers if you want to start more than one process. Yes, you shouldn't do that, but sometimes you do things you shouldn't do. 
- Forkel starts and monitors all programs entrusted to him.
- If one of the processes ends, for whatever reason, Forkel sends the configured signals to the remaining processes. These get the chance to terminate themselves correctly. 
- After the grace-period all processes are definitely terminated and the container terminates.
- In normal operation all received signals are passed on to all controlled processes.
- When entering "docker stop \<container\>" forkel ensures that each process receives the signal "15 -> SIGTerminated".

### operation
- All programs controlled by forkel must run in foreground
- Pass the configuration file with the switch -c. (forkel -c cfg.json)
- A sample configuration is in the repository.
- Use tini to start: ENTRYPOINT ["tini", "--", "/code/forkel", "-c", "/code/cfg2.json"]
- It relays all possible signals to all controlled processes.
- If a controlled process dies, the signals stored in the configuration (mostly 15) are sent to all remaining processes.
- Then forkel waits the time, which is stored in the configuration as grace-period, sends a kill -9 to all processes and terminates.
- loglevels: TRACE, DEBUG, INFO, WARN, ERROR, NONE
- Works as a simple zombie reaper when running on PID 1

### configuration (global)
    "grace-period":10,
    "handle-siginterrupt": 1,
    "loglevel":"DEBUG",
- grace-peroid: Waiting time until kill -9 is sent
- handle-siginterrupt: 1 -> CTRL-C on the console triggers a shutdown. 0 -> no handling
- loglevel: One of the loglevels

### configuration item (per program)
    {
      "executable":"/docker-signals/showsig",
      "name":"showsig 01",
      "parameter":["a", "b", "c"],
      "signal-blacklist":"02 17",
      "signal":9
    }

- executable: path to the executable
- name: name of the process in the process list
- parameter: comandline parameters
- signal-blacklist: Signals not send to the process (always two characters! e.g. 02)
- signal: signal send to the process, when a controlled process has died

### build
For containers the build should be static.
You need to install the library "libjsoncpp".

- for ubuntu: sudo apt install libjsoncpp-dev

- g++ -Wall -fexceptions -g  -c forkel.cpp -o forkel.o
- g++  -o forkel forkel.o  -static  -ljsoncpp -lpthread

### showsig
Showsig ist a little helper which is intercepting all possible signals and print them out when it receives one of them.
- https://github.com/dmaj/docker-signals.git
