#include "forkel.hpp"
#include <cstdarg>
#include <algorithm>
#include <future>
#include <iostream>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <assert.h>
#include <jsoncpp/json/json.h>
#include "logging.hpp"

using namespace std;
using namespace logging;


static shared_data* data1 = NULL;
static shared_data* data2 = NULL;

vector<TAppConfig> config;
int grace_period = 0;
int handle_SIGInterrupt = 0;
string sloglevel;
pid_t ppid;
pid_t kpid;





#define ARRAY_LEN(x)  (sizeof(x) / sizeof((x)[0]))

void empty_sig_handler ()
{
}

/**
    Defining signals to be handled.
    Not active in the moment.
    @param structure with informations needed by th mutex
    @return
*/
int x_configure_signals(sigset_t* const parent_sigset_ptr, const signal_configuration_t* const sigconf_ptr) {
	/* Block all signals that are meant to be collected by the main loop */
	if (sigfillset(parent_sigset_ptr)) {
		//PRINT_FATAL("sigfillset failed: '%s'", strerror(errno));
		return 1;
	}

	// These ones shouldn't be collected by the main loop
	uint i;
	int signals_for_tini[] = {SIGFPE, SIGSEGV, SIGBUS, SIGABRT, SIGTRAP, SIGSYS, SIGTTIN, SIGTTOU};
	for (i = 0; i < ARRAY_LEN(signals_for_tini); i++) {
		if (sigdelset(parent_sigset_ptr, signals_for_tini[i])) {
			//PRINT_FATAL("sigdelset failed: '%i'", signals_for_tini[i]);
			return 1;
		}
	}

	if (sigprocmask(SIG_SETMASK, parent_sigset_ptr, sigconf_ptr->sigmask_ptr)) {
		//PRINT_FATAL("sigprocmask failed: '%s'", strerror(errno));
		return 1;
    }

/*
  if (sigaction(SIGKILL, &empty_sig_handler, sigconf_ptr->sigttin_action_ptr)) {
		PRINT_FATAL("Failed to ignore SIGTTIN");
		return 1;
	}
*/
	return 0;
}

/**
    At th moment I'm using a signal handler.
    Maby it's better to use newer signal handling.
    Then there is no need for async singnal handling.
    Just wait for sigtimedwait.
    At the moment it's just for testing purposes.
    @param structure with informations needed by th mutex
    @return
*/
int x_testit ()
{

/* Configure signals */
	sigset_t parent_sigset, child_sigset;
	struct sigaction sigttin_action, sigttou_action;
	memset(&sigttin_action, 0, sizeof sigttin_action);
	memset(&sigttou_action, 0, sizeof sigttou_action);

	signal_configuration_t child_sigconf = {
		.sigmask_ptr = &child_sigset,
		.sigttin_action_ptr = &sigttin_action,
		.sigttou_action_ptr = &sigttou_action,
	};

	if (x_configure_signals(&parent_sigset, &child_sigconf)) {
		return 1;
	}


	return 0;
}



/**
    Mutex in shared memory. So it works also with forked processes.

    @param structure with informations needed by th mutex
    @return
*/
void initialise_shared(shared_data** data)
{
    // place our shared data in shared memory
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_SHARED | MAP_ANONYMOUS;
    *data = (shared_data*) mmap(NULL, sizeof(shared_data), prot, flags, -1, 0);
    assert(data);

    (*data)->done = false;

    // initialise mutex so it works properly in shared memory
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&(*data)->mutex, &attr);
}

/**
    Signalhadler. The code is protected by a mutex.
    So it schould be threadsafe.

    @param int containig the triggerd signal
    @return
*/
void sig_handler(int signo)
{
    pthread_mutex_lock(&data2->mutex);

    if ((handle_SIGInterrupt != 0) && (signo ==2))
        pthread_mutex_unlock(&data1->mutex);

    for (vector<TAppConfig>::const_iterator i = config.begin(); i != config.end(); ++i)
    {
        // Do not propagate blacklistet signale
        if (!check_blacklist ((*i).signal_blacklist, signo))
        {
            logging::TRACE(str_pf("dispatching signal %i to PID %i", signo,(*i).pid));
            kill((*i).pid, signo);
        }
    }
    pthread_mutex_unlock(&data2->mutex);
}

/**
    Getting all possible signals for handling

    @param
    @return
*/
void install_signalhandler(void (*handler) (int))
{

   int sig;
    for (sig = 1; sig < NSIG; sig++)
    {
        if (signal(sig, handler) == SIG_ERR) {
            logging::TRACE(str_pf ("Can't catch %i -> %s", sig, strsignal(sig)));
            //this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

/**
    Shutdown all startet processes

    @param
    @return
*/
void shutdown ()
{
    if (handle_SIGInterrupt != 0) kill(kpid, 9);

    cleanup ();

    logging::INFO(str_pf("Wait grace period: %i", grace_period));
    int status;
    chrono::steady_clock::time_point start = chrono::steady_clock::now();
    while (waitpid(-1, &status, WNOHANG) != -1)
    {
        chrono::seconds duration =  std::chrono::duration_cast<std::chrono::seconds>(chrono::steady_clock::now() - start );
        if (duration.count() > grace_period) break;
        usleep (1000);
    }

    cleanup (true);
}
/**
    Just wait indefinitely. However, the process is also monitored.
    If this process is actively terminated by releasing th mutex,
    then all processes are terminated and the program is terminated.
    We use it to trigger a shutdown from anywhere in the code.

    @param pid of the process (parent or forked)
    @return
*/
pid_t keepalive ()
{
    if (handle_SIGInterrupt == 0) return (getpid());;
    if ((kpid = fork()) < 0)
    {
        perror("fork");
    }
    else
    {
        if (ppid != getpid())
        {
            pthread_mutex_lock(&data1->mutex);
            //install_signalhandler(empty_sig_handler);
        }
    }
    return (getpid());

}

int main(int argc, char **argv, char **env)
{


    /* alternative signal handling; noch used
	sigset_t parent_sigset, child_sigset;
	struct sigaction sigttin_action, sigttou_action;
	memset(&sigttin_action, 0, sizeof sigttin_action);
	memset(&sigttou_action, 0, sizeof sigttou_action);

	signal_configuration_t child_sigconf = {
		.sigmask_ptr = &child_sigset,
		.sigttin_action_ptr = &sigttin_action,
		.sigttou_action_ptr = &sigttou_action,
	};
    */

    initialise_shared(&data1);
    initialise_shared(&data2);
    pthread_mutex_lock(&data1->mutex);
    ppid = getpid();
    logging::setLogLevel ("DEBUG");
    int c;
    string copt;
    while ( (c = getopt(argc, argv, "c:")) != -1) {
        switch (c) {
        case 'c':
            logging::TRACE (str_pf ("option c with value '%s'", optarg));
            copt = string(optarg);
            break;
        default:
            logging::DEBUG (str_pf ("?? getopt returned character code 0%o ??", c));
        }
    }
    if (optind < argc) {
        logging::DEBUG("non-option ARGV-elements: ");
        while (optind < argc)
        {
            logging::DEBUG(str_pf ("%s ", argv[optind++])); //this_thread::sleep_for(chrono::milliseconds(10));
        }
    }

    if (!read_config (copt))
    {
        logging::ERROR (str_pf ("config not found '%s'", &copt[0]));
        exit (1);
    };
    logging::setLogLevel (sloglevel);

    logging::TRACE("Start ..."); //this_thread::sleep_for(chrono::milliseconds(10));



    if (keepalive() != ppid) return 0;

    if (ppid == 1) logging::DEBUG ("running on PID 1 and reaping zombies");

    // Not for childs ... !
    if (forkel(config.size()) != ppid) return (0);


    install_signalhandler(sig_handler);
    /*
	if (configure_signals(&parent_sigset, &child_sigconf)) {
		return 1;
	}
	*/

    bool isZombie = true;
    do
    {
        isZombie = reap (waitpid (-1,0,WUNTRACED));
    } while (isZombie);

    shutdown ();

    return 0;
}
/**
    Reaping zombies

    @param
    @return
*/
bool reap (pid_t pid)
{
    /*
    // No signal handlin. Not used.
    siginfo_t sig;
    int ret1 = sigtimedwait(parent_sigset_ptr, &sig, &ts);
    if (ret1 > -1)
        printf ("Signal: %i\n", sig.si_signo);

    sleep (1);
    */

    if (pid == kpid)
    {
        logging::TRACE ("keepalive hat sich beendet");
        return false;
    }
    for (uint index = 0; index < config.size(); ++index)
        if (config[index].pid == pid) return false;

    logging::TRACE(str_pf ("Reaping zombie PID: %i", pid));
    return (true);

}



/**
    Sendig the signal defined in the config, or kill all supevised processes

    @param bool; true: send 9, false send signal defined in config
*/
void cleanup(bool killall)
{
  for (vector<TAppConfig>::const_iterator i = config.begin(); i != config.end(); ++i)
    {
        pid_t pid = (*i).pid;
        int signal = (killall) ? 9 : (*i).signal;
        int ret = kill(pid, signal);
        if (ret != -1)
            logging::INFO(str_pf("Send signal %i to PID %i with result %i", signal, pid, ret));
        else
            logging::DEBUG(str_pf("Send signal %i to PID %i with result %i", signal, pid, ret));
    }

}



/**
    Checking whether a signal ist blacklistet for an process

    @param string with blacklistet signals
    @param int with the index of the process
    @return boolean; blacklistet or not
*/
bool check_blacklist (string signal_blacklist, int signal)
{
    string str_signal = to_string(signal);
    if (signal < 10) str_signal = "0" + str_signal;
    size_t found = signal_blacklist.find(str_signal);
    if (found==std::string::npos) return false;
    return true;
}

/**
    Reading the json config file

    @param string containing the json file
    @return boolean, which shows whether the read was successful or not
*/
bool read_config(string copt)
{
    Json::Reader reader;
    Json::Value root;

    std::ifstream cfgfile;
    cfgfile.open(copt);
    if (!cfgfile.is_open()) return false;
    reader.parse(cfgfile, root, false);
    grace_period = root["grace-period"].asInt();
    handle_SIGInterrupt = root["handle-siginterrupt"].asInt();
    sloglevel = root["loglevel"].asString();
    Json::Value entriesArray = root["apps"];

    Json::Value::const_iterator i;
    for (i = entriesArray.begin(); i != entriesArray.end(); ++i)
    {
        TAppConfig appConfig;
        appConfig.executable = (*i)["executable"].asString();
        appConfig.name = (*i)["name"].asString();
        for (Json::Value::ArrayIndex j = 0; j != (*i)["parameter"].size(); j++)
            appConfig.parameter.push_back(((*i)["parameter"][j]).asString());
        appConfig.signal_blacklist = (*i)["signal-blacklist"].asString();
        appConfig.signal = (*i)["signal"].asInt();
        config.push_back(appConfig);
    }
    return true;
}

/**
    Startting processed definde in th config

    @param
    @return
*/
pid_t forkel(int nprocesses)
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
            logging::DEBUG(str_pf("Executable: %s   Name: %s   ParentPID: %i   PID: %i", executable, param_list[0], ppid, cpid));
            int ret = execv(executable, param_list);
            if (ret == -1) logging::ERROR(str_pf("Start of %s failed", param_list[0]));
            //exit(-1);
        }
        else if(pid > 0)
        {
            //parent
            config[nprocesses - 1].pid = pid;
            logging::TRACE (str_pf("Setting pid %i for process %i", pid, nprocesses - 1));
            //Every process gets his own pid as process group
            setpgid (pid, pid);
            //setpgid (0,0);
            forkel(nprocesses - 1);
        }
    }
    return (getpid());
}

/**
    Formating printf style in a string
    Used for logging

    @param printf style input
    @return formattetd sting
*/

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
