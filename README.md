# forkel

Please note: This program has been written in a very short time and not yet as I would like it to be. But at least it runs stable.
This program supports the handling of signals in containers if you want to start more than one process. Yes, you shouldn't do that, but sometimes you do things you shouldn't do. 

### operation
- All programs controlled by forkel must run in foreground
- Pass the configuration file with the switch -c. (forkel -c cfg.json)
- A sample configuration is in the repository.
- Use tini to start: ENTRYPOINT ["tini", "--", "/code/forkel", "-c", "/code/cfg2.json"]
- It relays all possible signals to all controlled processes.
- If a controlled process dies, the signals stored in the configuration (mostly 15) are sent to all remaining processes.
- Then forkel waits the time, which is stored in the configuration as grace-period, sends a kill -9 to all processes and terminates.

### configuration item
<?
    {
      "executable":"/home/dieter/docker/signals/docker-signals/showsiga",
      "name":"showsig 01",
      "parameter":["a", "b", "c"],
      "signal":9
    }
?>

### build
For containers the build should be static.
You need to install the library "libjsoncpp".

- for ubuntu: sudo apt install libjsoncpp-dev

- g++ -Wall -fexceptions -g  -c main.cpp -o main.o
- g++  -o forkel main.o  -static  -ljsoncpp

### why forkel?
- There are a lot of programs that manage processes in containers. There are s6, supervisor, runit, ...
- but ...
- They are too big, the configuration is too complex, they want to run as root, they don't do what they are supposed to do, ...
- Forkel is small, static and minimal.
