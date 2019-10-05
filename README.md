# forkel

Please note: This program has been written in a very short time and not yet as I would like it to be. But at least it runs stable.
This program supports the handling of signals in containers if you want to start more than one process. Yes, you shouldn't do that, but sometimes you do things you shouldn't do. 

### Editor.md



### build
For containers the build should be static.
You need to install the library "libjsoncpp".

- g++ -Wall -fexceptions -g  -c main.cpp -o main.o
- g++  -o forkel main.o  -static  -ljsoncpp
