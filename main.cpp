#include <cstdarg>
#include <string.h>
#include <algorithm>
#include <sys/wait.h>
#include <unistd.h>
#include <jsoncpp/json/json.h>
#include "logging.hpp"

using namespace std;
using namespace logging;



struct TAppConfig
{
    string executable;
    string name;
    vector<string> parameter;
    int signal;
    pid_t pid;
};

vector<TAppConfig> config;
int grace_period;
string loglevel;
pid_t ppid;

void forkel (int);
void cleanup (bool killall = false);
void displayCfg(const Json::Value &cfg_root);
void read_config (string copt);
void sig_handler(int signo);
string str_pf (string format, ...);
void printVector();
void testExec(int);

int main(int argc, char **argv, char **env)
{
    ppid = getpid();
    logging::setLogLevel ("DEBUG");
    int c;
    string copt = "";
    while ( (c = getopt(argc, argv, "c:")) != -1) {
        switch (c) {
        case 'c':
            logging::DEBUG (str_pf ("option c with value '%s'\n", optarg));
            copt = optarg;
            break;
        default:
            logging::DEBUG (str_pf ("?? getopt returned character code 0%o ??\n", c));
        }
    }
    if (optind < argc) {
        logging::DEBUG("non-option ARGV-elements: ");
        while (optind < argc)
        {
            logging::DEBUG(str_pf ("%s ", argv[optind++])); //this_thread::sleep_for(chrono::milliseconds(10));
        }
    }

    read_config (copt);
    logging::setLogLevel (loglevel);

    logging::TRACE("Start ..."); //this_thread::sleep_for(chrono::milliseconds(10));

    int sig;
    for (sig = 1; sig < NSIG; sig++)
    {
        if (signal(sig, sig_handler) == SIG_ERR) {
            logging::TRACE(str_pf ("Can't catch %i -> %s", sig, strsignal(sig)));
            //this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    forkel(config.size());

    // Not for childs ... !
    if (ppid != getpid()) return (0);

    wait (0);

    cleanup ();

    logging::INFO(str_pf("Wait grace period: %i\n", grace_period));
    int status;

    chrono::steady_clock::time_point start = chrono::steady_clock::now();
    while (waitpid(0, &status, WNOHANG) != -1)
    {
        chrono::seconds duration =  std::chrono::duration_cast<std::chrono::seconds>(chrono::steady_clock::now() - start );
        if (duration.count() > grace_period) break;
        usleep (100);
    }

    cleanup (true);

    return 0;
}

void cleanup(bool killall)
{
  for (vector<TAppConfig>::const_iterator i = config.begin(); i != config.end(); ++i)
    {
        pid_t pid = (*i).pid;
        int signal = (killall) ? 9 : (*i).signal;
        int ret = kill(pid, signal);
        if (ret != -1)
            logging::INFO(str_pf("Send signal %i to PID %i with result %i\n", signal, pid, ret));
        else
            logging::DEBUG(str_pf("Send signal %i to PID %i with result %i\n", signal, pid, ret));
    }

}


void sig_handler(int signo)
{
  for (std::vector<TAppConfig>::const_iterator i = config.begin(); i != config.end(); ++i)
    {
        pid_t pid = (*i).pid;
        kill(pid, signo);
    }
}



void read_config(string copt)
{
    Json::Reader reader;
    Json::Value root;

    std::ifstream cfgfile(copt);
    reader.parse(cfgfile, root, false);
    grace_period = root["grace-period"].asInt();
    loglevel = root["loglevel"].asString();
    Json::Value entriesArray = root["apps"];

    Json::Value::const_iterator i;
    for (i = entriesArray.begin(); i != entriesArray.end(); ++i)
    {
        TAppConfig appConfig;
        appConfig.executable = (*i)["executable"].asString();
        appConfig.name = (*i)["name"].asString();
        for (Json::Value::ArrayIndex j = 0; j != (*i)["parameter"].size(); j++)
            appConfig.parameter.push_back(((*i)["parameter"][j]).asString());
        //convertVtoC (appConfig.parameter);
        appConfig.signal = (*i)["signal"].asInt();
        config.push_back(appConfig);
    }
}

void forkel(int nprocesses)
{
    pid_t pid;
    pid_t cpid;
    pid_t ppid;

    ppid = getpid();

    if(nprocesses > 0)
    {
        if ((pid = fork()) < 0)
        {
            perror("fork");
        }
        else if (pid == 0)
        {
            //Child stuff here
            cpid = getpid();
            TAppConfig cfg = config[nprocesses - 1];
            char* executable = &(cfg.executable[0]);

            vector<const char *> chars(cfg.parameter.size());
            transform(cfg.parameter.begin(), cfg.parameter.end(), chars.begin(), mem_fun_ref(&string::c_str));

            char* param_list[cfg.parameter.size()+2];
            param_list[0] = &(cfg.name[0]);
            for (uint cntr = 0; cntr < chars.size(); cntr++)
                param_list[cntr + 1] = (char*) chars[cntr];
            param_list[cfg.parameter.size() + 1] = 0;
            logging::DEBUG(str_pf("Executable: %s   Name: %s   ParentPID: %i   PID: %i\n", executable, param_list[0], ppid, cpid));

            int ret = execv(executable, param_list);
            if (ret == -1) logging::ERROR(str_pf("Start of %s failed\n", param_list[0]));
            //exit(-1);
        }
        else if(pid > 0)
        {
            //parent
            config[nprocesses - 1].pid = pid;
            forkel(nprocesses - 1);
        }
    }
}

string str_pf(const string fmt_str, ...) {
    int final_n, n = ((int)fmt_str.size()) * 2; /* Reserve two times as much as the length of the fmt_str */
    std::unique_ptr<char[]> formatted;
    va_list ap;
    while(1) {
        formatted.reset(new char[n]); /* Wrap the plain char array into the unique_ptr */
        strcpy(&formatted[0], fmt_str.c_str());
        va_start(ap, fmt_str);
        final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
        va_end(ap);
        if (final_n < 0 || final_n >= n)
            n += abs(final_n - n + 1);
        else
            break;
    }
    return std::string(formatted.get());
}

void testExec (int num)
{
    TAppConfig cfg = config[num - 1];
    char* executable = &(cfg.executable[0]);
    char* name = &(cfg.name[0]);

    vector<const char *> chars(cfg.parameter.size());
    transform(cfg.parameter.begin(), cfg.parameter.end(), chars.begin(), mem_fun_ref(&string::c_str));

    char* result[cfg.parameter.size()+2];
    result[0] = name;
    for (uint cntr = 0; cntr < chars.size(); cntr++)
        result[cntr + 1] = (char*) chars[cntr];
    result[cfg.parameter.size() + 1] = 0;

    execv(executable, result);
}


void printVector ()
{
  for (std::vector<TAppConfig>::const_iterator i = config.begin(); i != config.end(); ++i)
    {
        std::cout << (*i).executable << '\n';
        std::cout << (*i).name << '\n';
        //std::cout << (*i).parameter << '\n';
    }

}
