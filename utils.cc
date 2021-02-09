
#include "server.h"
#include <stdio.h>      
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h> 
#include <string.h> 
#include <arpa/inet.h>
#include "tokenize.h"

/*
 * Log file
 */

const char* logfile = "server.log";
const char* pidfile = "server.pid";
const char* config = "config";

/*
 * true iff the file server is alive (and kicking).
 */
bool falive;

pthread_mutex_t logger_mutex;

threadpool_t *t_pool;
int t_incr = 0;
int t_max = 20;
extern struct sync_server* replica_server;
// int rep_serv_count;

extern char **environ;
int message_inc_number;

/*
 * What to debug (nothing by default):
 */
bool debugs[3] = {false, false, false};

void logger(const char * msg) {
    pthread_mutex_lock(&logger_mutex);
    time_t tt = time(0);
    char* ts = ctime(&tt);
    ts[strlen(ts) - 1] = '\0';
    printf("%s: %s", ts, msg);
    fflush(stdout);
    pthread_mutex_unlock(&logger_mutex);
}


void sigquit_handler(int s){
    char msg[MAX_LEN];
    snprintf(msg, MAX_LEN, "\nSIGQUIT signal received: Terminating %d active threads and the server\n" ,t_pool->active);
    logger(msg);
    for(int i=0;i<(t_pool->t_incr+t_pool->max_size);i++){
            if(&(t_pool->clients[i]) && t_pool->clients[i].status == running){
                t_pool->clients[i].shutdown = 1;
            }
        }
    exit(0);
}

void sigup_handler(int s){
    char msg[MAX_LEN];
    snprintf(msg, MAX_LEN, "SIGHUP signal received: Terminating %d active threads but the server is still running....\n" ,t_pool->active);
    logger(msg);
    for(int i=0;i<(t_pool->t_incr+t_pool->max_size);i++){
            if(&(t_pool->clients[i]) && t_pool->clients[i].status == running){
                t_pool->clients[i].shutdown = 1;
            }
        }

    delete t_pool;
    if((t_pool = (threadpool_t *)malloc(sizeof(threadpool_t))) == NULL) {
            snprintf(msg, MAX_LEN, "%s: bb server pthread_create error: %s\n", __FILE__, strerror(3));
            logger(msg);
    }
    t_pool->clients = (client_t *)malloc(sizeof(client_t) * (t_incr+t_max));

    if((pthread_mutex_init(&(t_pool->lock), NULL) != 0)){
        snprintf(msg, MAX_LEN, "%s: bb server pthread_mutex_init error: %s\n", __FILE__, strerror(3));
        logger(msg);
    }
    t_pool->count =0;
    t_pool->max_size =t_max;
    t_pool -> t_incr = t_incr;
    t_pool->active = 0;

    for(int i=0; i < t_incr;i++){
        pthread_cond_init(&(t_pool->clients[i].thread_cond),NULL);
        t_pool->clients[i].status = idle;
        if ( pthread_create(&(t_pool->clients[i].thread), NULL, (void* (*) (void*))bulletin_board_server_client, (void*)&(t_pool->clients[i])) != 0 ) {
            snprintf(msg, MAX_LEN, "%s: bb server pthread_create: %s\n", __FILE__, strerror(errno));
            logger(msg);
            snprintf(msg, MAX_LEN, "%s: the bb server died.\n", __FILE__);
            logger(msg);
            falive = false;
            return ;
        }
        t_pool->clients[i].lock = t_pool->lock;
        t_pool->count++;
    }

}


void* threads_monitor(threadpool_t* t_pool){
    int active_threads = 0, idle_threads =0;
    char msg[MAX_LEN];
    while(1){
        sleep(20);
        active_threads = 0, idle_threads =0;
        for(int i=0;i<(t_pool->t_incr+t_pool->max_size);i++){
            if(&(t_pool->clients[i])){
                switch(t_pool->clients[i].status){
                    case idle: idle_threads++; break;
                    case recreate:
                        pthread_cond_init(&(t_pool->clients[i].thread_cond),NULL);
                        t_pool->clients[i].status = idle;
                        if ( pthread_create(&(t_pool->clients[i].thread), NULL, (void* (*) (void*))bulletin_board_server_client, (void*)&(t_pool->clients[i])) != 0 ) {
                            snprintf(msg, MAX_LEN, "%s: bb server pthread_create: %s\n", __FILE__, strerror(errno));
                            logger(msg);
                            snprintf(msg, MAX_LEN, "%s: the bb server died.\n", __FILE__);
                            logger(msg);
                            falive = false;
                                    return 0;
                        }
                        t_pool->clients[i].lock = t_pool->lock;
                        idle_threads++;
                        break;
                    case deactivate:
                        pthread_cancel(t_pool->clients[i].thread);
                        break;
                    case running: active_threads++; break;
                }
                t_pool -> count = idle_threads+active_threads;
                t_pool -> active = active_threads;
            }

        }
        

        if((idle_threads>= t_pool->t_incr )&& (t_pool->count>t_pool->t_incr)){
           snprintf(msg, MAX_LEN, "%s: And its time to deallocate threads\n", __FILE__);
           logger(msg); 
           for(int i=0;i<(t_pool->t_incr+t_pool->max_size);i++){
                if(&(t_pool->clients[i]) && t_pool -> clients[i].status == idle){
                    t_pool -> clients[i].status = deactivate;
                    idle_threads--;
                }

            }  
        }

        snprintf(msg, MAX_LEN, "%s: threads status idle : %d , active : %d\n", __FILE__, idle_threads ,active_threads);
        logger(msg);

    }
    return 0;
}



/*
 * Simple conversion of IP addresses from unsigned int to dotted
 * notation.
 */
void ip_to_dotted(unsigned int ip, char* buffer) {
    char* ipc = (char*)(&ip);
    sprintf(buffer, "%d.%d.%d.%d", ipc[0], ipc[1], ipc[2], ipc[3]);
}

int next_arg(const char* line, char delim) {
    int arg_index = 0;
    char msg[MAX_LEN];  // logger string

    // look for delimiter (or for the end of line, whichever happens first):
    while ( line[arg_index] != '\0' && line[arg_index] != delim)
        arg_index++;
    // if at the end of line, return -1 (no argument):
    if (line[arg_index] == '\0') {
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: next_arg(%s, %c): no argument\n", __FILE__, line ,delim);
            logger(msg);
        } /* DEBUG_COMM */
        return -1;
    }
    // we have the index of the delimiter, we need the index of the next
    // character:
    arg_index++;
    // empty argument = no argument...
    if (line[arg_index] == '\0') {
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: next_arg(%s, %c): no argument\n", __FILE__, line ,delim);
            logger(msg);
        } /* DEBUG_COMM */    
        return -1;
    }
    if (debugs[DEBUG_COMM]) {
        snprintf(msg, MAX_LEN, "%s: next_arg(%s, %c): split at %d\n", __FILE__, line ,delim, arg_index);
        logger(msg);
    } /* DEBUG_COMM */
    return arg_index;
}

void* bulletin_board_server (int msock) {
    int ssock;                      // slave sockets
    struct sockaddr_in client_addr; // the address of the client...
    socklen_t client_addr_len = sizeof(client_addr); // ... and its length
    // Setting up the thread creation:
    pthread_t monitor_thread;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);
    // bool found_thread = false;

    char msg[MAX_LEN];  // logger string

    if((t_pool = (threadpool_t *)malloc(sizeof(threadpool_t))) == NULL) {
            goto err;
    }
    t_pool->clients = (client_t *)malloc(sizeof(client_t) * (t_incr+t_max));

    if((pthread_mutex_init(&(t_pool->lock), NULL) != 0)){
        goto err;
    }
    t_pool->count =0;
    t_pool->max_size =t_max;
    t_pool -> t_incr = t_incr;
    t_pool->active = 0;

    for(int i=0; i < t_incr;i++){
        pthread_cond_init(&(t_pool->clients[i].thread_cond),NULL);
        t_pool->clients[i].status = idle;
        if ( pthread_create(&(t_pool->clients[i].thread), &ta, (void* (*) (void*))bulletin_board_server_client, (void*)&(t_pool->clients[i])) != 0 ) {
            snprintf(msg, MAX_LEN, "%s: Bulletin board server pthread_create: %s\n", __FILE__, strerror(errno));
            logger(msg);
            snprintf(msg, MAX_LEN, "%s: the Bulletin board server died.\n", __FILE__);
            logger(msg);
            falive = false;
            return 0;
        }
        t_pool->clients[i].lock = t_pool->lock;
        t_pool->count++;
    }

    if ( pthread_create(&monitor_thread, &ta, (void* (*) (void*))threads_monitor, (void*)t_pool) != 0 ) {
                snprintf(msg, MAX_LEN, "%s: monitor pthread_create: %s\n", __FILE__, strerror(errno));
                logger(msg);
                return 0;
    }

    while (1) {
        // Accept connection:
        ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (ssock < 0) {
            if (errno == EINTR) continue;
            snprintf(msg, MAX_LEN, "%s: Bulletin board server accept: %s\n", __FILE__, strerror(errno));
            logger(msg);
            snprintf(msg, MAX_LEN, "%s: the Bulletin board server died.\n", __FILE__);
            logger(msg);
            falive = false;
            return 0;
        }
        if(t_pool->active<t_pool->count){
            for(int i=0;i<t_pool-> count;i++){
                if(t_pool->clients[i].status == idle || t_pool->clients[i].status == deactivate){
                snprintf(msg, MAX_LEN, "Found an idle thread\n");
                logger(msg);
                    t_pool->clients[i].sd = ssock;
                    ip_to_dotted(client_addr.sin_addr.s_addr, t_pool->clients[i].ip);
                    if( pthread_cond_signal(&(t_pool->clients[i].thread_cond)) != 0) {
                        // err = threadpool_lock_failure;
                            snprintf(msg, MAX_LEN, "it is working: %d\n", 2);
                            logger(msg);
                         }
                         // found_thread = true;
                         t_pool->clients[i].status = running;
                    break;
                }
            }
        }else if(t_pool->count < t_pool->max_size){
                snprintf(msg, MAX_LEN, "Its time to create new bunch of threads i.e \ncurrent count is: %d\nmax_allowed is: %d\n bunch size is: %d\n",t_pool->count,t_pool->max_size,t_pool->t_incr);
                
            }else{

                snprintf(msg, MAX_LEN,"FAIL 2 All threads are busy, try again later\n");
                logger(msg);
                send(ssock, msg, strlen(msg),0);
                close(ssock);
            }
        
    }

err:
    if(t_pool) {
        // threadpool_free(pool);
    }

    return 0;  
}



void set_up_sequencer(){

    message_inc_number = 1;
    char* stuff = new char[MAX_LEN];
    while(readline(bbfd, stuff , MAX_LEN) >0){
        message_inc_number++;
    }
    printf("Set Sequence at: %d\n", message_inc_number);


}


void* sync_server_handler(int msock){

    int ssock;                      // slave sockets
    struct sockaddr_in client_addr; // the address of the client...
    socklen_t client_addr_len = sizeof(client_addr); // ... and its length
    // Setting up the thread creation:
    pthread_t tt;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    char msg[MAX_LEN];  // logger string

    while (1) {
        // Accept connection:
        //listen(1);
        ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (ssock < 0) {
            if (errno == EINTR) continue;
            snprintf(msg, MAX_LEN, "%s: SYNC SERVER: Error in accepting connection: %s\n", __FILE__, strerror(errno));
            logger(msg);
            return 0;
        }

        // assemble client coordinates (communication socket + IP)
        snprintf(msg, MAX_LEN,"SYNC SERVER: Request for a new Sync Server");
        client_t* clnt = new client_t;
        clnt -> sd = ssock;
        ip_to_dotted(client_addr.sin_addr.s_addr, clnt -> ip);


        if(ssock > 0){
            // create a new thread for the incoming client:
            if ( pthread_create(&tt, &ta, (void* (*) (void*))sync_server_client, (void*)clnt) != 0 ) {
            snprintf(msg, MAX_LEN, "%s: SYNC SERVER: Unable to create pthread: %s\n", __FILE__, strerror(errno));
            logger(msg);
            return 0;

        }
    }
        // go back and block on accept.
    }
    return 0;   // will never reach this anyway...
}





/*
 * Initializes the access control structures, fires up a thread that
 * handles the file server, and then does the standard job of the main
 * function in a multithreaded shell server.
 */
int main (int argc, char** argv, char** envp) {
    int bbport = 9000;              // ports to listen to
    int repport = 10000;  // informational use only.

    long int bbsock,syncsock;              // master sockets
    const int qlen = 32;            // queue length for incoming connections
    char* progname = basename(argv[0]);
    char bbfile[] = "bbfile.txt";
    bool detach = true;

    char msg[MAX_LEN];  // logger string


    char command[129];   // buffer for commands
    command[128] = '\0';
    char* com_tok[129];  // buffer for the tokenized commands
    int num_tok;
    int confd = open(config, O_RDONLY,S_IRUSR | S_IWUSR);
    if (confd > 0) {
        sprintf(msg,"opened file:%s \n",config);
        logger(msg);
        while (readline(confd, command, 128)> 0) {
            sprintf(msg,"\nFound command:%s \n",command);
            logger(msg);
            num_tok = str_tokenize(command, com_tok, strlen(command),'=');
            if (strcmp(com_tok[0], "THMAX") == 0 && atol(com_tok[1]) > 0){
                t_max = atol(com_tok[1]);
            }
            else if (strcmp(com_tok[0], "BBPORT") == 0 && atol(com_tok[1]) > 0){
                bbport = (unsigned short)atol(com_tok[1]);
            }
            // remote host and port
            else if (strcmp(com_tok[0], "SYNCPORT") == 0 && atoi(com_tok[1]) > 0){
                repport = (unsigned short)atoi(com_tok[1]);
            }
            else if (strcmp(com_tok[0], "BBFILE") == 0) {
                // strncpy(bbfile,com_tok[1],128);
                snprintf(bbfile,MAX_LEN,"%s",com_tok[1]);
            }
            else if (strcmp(com_tok[0], "PEERS") == 0){
                rep_serv_count = 0;
                replica_server = (sync_server*)malloc(sizeof(sync_server)*5);
                char* rep_conf[129];
                num_tok = str_tokenize(com_tok[1], rep_conf, strlen(com_tok[1]),' ');
                if(num_tok > 0){
                    for(int j=0;j<num_tok;j++){
                        snprintf(msg, MAX_LEN,"\nFound peers %d)%s \n",j,rep_conf[j]);
                        logger(msg);
                        char *ptr = strtok(rep_conf[j],":");
                        snprintf(msg, MAX_LEN, "IP : %s\t",ptr);
                        if(ptr!=NULL){
                            replica_server[rep_serv_count].ip = ptr;
                            ptr = strtok(NULL,":");
                            printf("Port : %s\n",ptr);

                            if(ptr!=NULL){
                                replica_server[rep_serv_count].port = atoi(ptr);
                                rep_serv_count++;
                            }
                            else{
                                break;
                                rep_serv_count = 0;
                            }
                        }else{  
                            break;
                                rep_serv_count = 0;
                        }

                    }

                     

                }

            }
            // remote host and port
            else if (strcmp(com_tok[0], "DAEMON") == 0 && atoi(com_tok[1])>-1){
                detach = atoi(com_tok[1]);
            }
            else if (strcmp(com_tok[0], "DEBUG") == 0 && atoi(com_tok[1])>-1) {
                debugs[DEBUG_DELAY] = atoi(com_tok[1]);
            }
        }
        close(confd);

    }else{
        perror("\nconfig file");
        snprintf(msg, MAX_LEN,"\nUnable to open config file\n");
    }

    sprintf(msg,"SERVER CONFIG: BP = %d, SP = %d, t_max = %d, bbfile = %s,detach = %d\n",bbport,repport,t_max,bbfile,detach);
    logger(msg);


    // read terminal size and remote information
    










    pthread_mutex_init(&logger_mutex, 0);
    
    debugs[DEBUG_COMM] = debugs[DEBUG_FILE] = 1;

    // parse command line

    extern char *optarg;
    int copt;
      // Detach by default
    while ((copt = getopt (argc,argv,"s:d:T:b:f:p:")) != -1) {
        switch ((char)copt) {
        case 'd':
            debugs[DEBUG_DELAY] = 1;
            snprintf(msg, MAX_LEN, "BULLETIN BOARD SERVER: Opened with delay.\n");
            break;
            
        case 'f':
            detach = false;
            break;
        case 's':
            repport = atoi(optarg);
            
            if(optind<argc){
                delete replica_server;
                replica_server = (sync_server*)malloc(sizeof(sync_server)*5);
                rep_serv_count = 0;
                do{
                    char *ptr = strtok(argv[optind],":");
                    if(ptr!=NULL){
                        sprintf(replica_server[rep_serv_count].ip,"%s",ptr);
                        ptr = strtok(NULL,":");
                        if(ptr!=NULL){
                            replica_server[rep_serv_count].port = atoi(ptr);
                            rep_serv_count++;
                        }
                        else{
                            break;
                            rep_serv_count = 0;
                        }
                    }else{
                        break;
                            rep_serv_count = 0;
                    }
                    optind++;
                }while(optind<argc);
            }
                        
            break;
        case 'b':
            // bbfile = optarg;

            // strcpy(bbfile,optarg);
            sprintf(bbfile,"%s",optarg);
            snprintf(msg, MAX_LEN, "Given input is %s\n", bbfile);

            break;
        case 'p':
            bbport = atoi(optarg);
            break;
        case 'T':
            t_max = atoi(optarg);
            break;            
        }
    }
    t_incr = t_max;
    if (bbport <= 0 ||t_max ==0  || repport ==0 ) {
        snprintf(msg, MAX_LEN, "Usage: %s [-d] [-D] [-b <bbfile name>] [-p <file server port>] [-T <Max thread pool size>] [-s <replication server port>] <IP address:port> [list Sync servers].\n", progname);
        return 1;
    }
    

    detach = false;
    logger(msg);

    snprintf(msg,MAX_LEN,"Sync servers Given: %d \n", rep_serv_count);
    logger(msg);
    for(int y =0 ;y<rep_serv_count;y++){
        snprintf(msg,MAX_LEN,"%d ) %s host %d  \n", (y+1),replica_server[y].ip,replica_server[y].port);
        logger(msg);
    }

    snprintf(msg, MAX_LEN, "\nBulletin board file being used: %s\n\n\n", bbfile);
    bbfd = -1;
    bbfd = open(bbfile, O_RDWR|O_APPEND|O_CREAT, S_IRUSR | S_IWUSR);
    if (bbfd < 0) {
        perror("FILE ERROR: Error in openig the file");
        snprintf(msg, MAX_LEN,"Will not write the PID.\n");
        return 0;


    }

    set_up_sequencer();

    snprintf(msg, MAX_LEN, "\nBULLETIN BOARD SERVER: Opened Bulletin Board File (%s) successfully with descriptor %d",bbfile,bbfd);


    // The pid file does not make sense as a lock file since our
    // server never goes down willlingly.  So we do not lock the file,
    // we just store the pid therein.  In other words, we hint to the
    // existence of a pid file but we are not really using it.
    int pifd = open(pidfile, O_RDWR| O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (pifd < 0) {
        perror("FILE ERROR: Unable to open the file.\t");
        snprintf(msg, MAX_LEN,"Cannot write the PID.\n");
    }
    snprintf(msg, MAX_LEN, "%d\n", getpid());
    write(pifd, msg, strlen(msg));
    close(pifd);

    // Initialize the file locking structure:
    flock_size = getdtablesize();
    flocks = new rwexcl_t*[flock_size];
    for (size_t i = 0; i < flock_size; i++)
        flocks[i] = 0;
    rwexcl_t* lck = new rwexcl_t;
    pthread_mutex_init(&lck -> mutex, 0);
    pthread_cond_init(&lck -> can_write,0);
    lck -> reads = 0;
    lck -> fd = bbfd;
    lck -> owners = 1;
    lck -> name = new char[strlen(bbfile) + 1];
    strcpy(lck -> name, bbfile);

    flocks[bbfd] = lck;

    bbsock = passivesocket(bbport,qlen);
    if (bbsock < 0) {
        perror("BULLETIN BOARD SERVER: Failed to create the socket.");
        return 1;
    }
   snprintf(msg, MAX_LEN,"Bulletin Board Server is working on port: %d\n", bbport);
    
    syncsock = passivesocket(repport,qlen);
    if (syncsock < 0) {
        perror("SYNC SERVER: Failed to create the socket.\n");
        return 1;
    }
    snprintf(msg, MAX_LEN, "Sync Server is working on port: %d\n", repport);

    signal(SIGHUP,  sigup_handler);
    signal(SIGQUIT,  sigquit_handler);
    // ... and we detach!
    if (detach) {
        // umask:
        umask(0177);

        // ignore SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGALRM, SIGSTOP:
        // (we do not need to do anything about SIGTSTP, SIGTTIN, SIGTTOU)
        signal(SIGINT,  SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        signal(SIGALRM, SIG_IGN);
        signal(SIGSTOP, SIG_IGN);

        // private group:
        setpgid(getpid(),0);

        // close everything (except the master socket) and then reopen what we need:
        for (int i = getdtablesize() - 1; i >= 0 ; i--)
            if (i != syncsock && i != bbsock)
                close(i);
        // stdin:
        int fd = open("/dev/null", O_RDONLY);
        // stdout:
        fd = open(logfile, O_WRONLY|O_CREAT|O_APPEND,S_IRUSR|S_IWUSR);
        // stderr:
        dup(fd);

        // we detach:
        fd = open("/dev/tty",O_RDWR);
        ioctl(fd,TIOCNOTTY,0);
        close(fd);

        // become daemon:
        int pid = fork();
        if (pid < 0) {
            perror("PID: Unable to fork");
            return 1;
        }
        if (pid > 0) return 0;  // parent dies peacefully
        // and now we are a real server.
    }

    // Setting up the thread creation:
    pthread_t tt;
    pthread_t rst;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    // Launch the thread that becomes a file server:
    if ( pthread_create(&tt, &ta, (void* (*) (void*))bulletin_board_server, (void*)bbsock) != 0 ) {
        snprintf(msg, MAX_LEN, "%s: Unable to create pthread: %s\n", __FILE__, strerror(errno));
        logger(msg);
        return 1;
    }



    // Launch the thread that becomes a replica server:
    if ( pthread_create(&rst, NULL, (void* (*) (void*))sync_server_handler, (void*)syncsock) != 0 ) {
        snprintf(msg, MAX_LEN, "%s: Unable to create pthread: %s\n", __FILE__, strerror(errno));
        logger(msg);
        return 1;
    } 


    falive = true;

    // keep this thread alive for the file server
    while (falive) {
        sleep(30);
    }

    // close(bbfd);

    snprintf(msg, MAX_LEN, "%s: BULLETIN BOARD SERVER: Exited as all the servers have died.\n", __FILE__);
    logger(msg);
    
    return 1;
}
