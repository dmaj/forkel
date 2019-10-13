#ifndef __FORKEL_HPP__
#define __FORKEL_HPP__

#include <string>
#include <cstdarg>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <jsoncpp/json/json.h>

using namespace std;

// New signal handling. Not used.
//static struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };

struct TAppConfig
{
    string executable;
    string name;
    string signal_blacklist = "";
    vector<string> parameter;
    int signal;
    pid_t pid;
};

typedef struct
{
  bool done;
  pthread_mutex_t mutex;
} shared_data;

typedef struct {
   sigset_t* const sigmask_ptr;
   struct sigaction* const sigttin_action_ptr;
   struct sigaction* const sigttou_action_ptr;
} signal_configuration_t;


pid_t forkel (int);
void cleanup (bool killall = false);
void displayCfg(const Json::Value &cfg_root);
bool read_config (string copt);
void empty_handler(int);
bool check_blacklist (string, int);
string str_pf (string format, ...);
bool reap (pid_t);


#endif //__FORKEL_HPP__
