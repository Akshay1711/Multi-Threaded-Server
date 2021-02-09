/*
 * Part of the solution for Assignment 3, by Stefan Bruda.
 *
 * This file contains the code for the file server.
 */

#include "server.h"
#include <arpa/inet.h> 
#include <stdlib.h>

/*
 * The access control structure for the opened files (initialized in
 * the main function), and its size.
 */
rwexcl_t** flocks;
size_t flock_size;

bool* open_fds;

int bbfd;


sync_server* replica_server;
int rep_serv_count;



int phase_two_commit(int reqType,const char* input,int msg_id){
    char msg[MAX_LEN];
    snprintf(msg,MAX_LEN,"SYNC SERVER: Syncing data to Replication servers.\n");
    logger(msg);
    int y=0;
    printf("Inputs given to this function: reqType = %d, input = %s, message num = %d rep ip: %s\n", reqType, input, msg_id, replica_server[y].ip);
    int success = 1;
    int rep_fds[MAX_LEN];
    
    //printf("%s\n server ip address in server.cc is ",replica_server[y].ip);
    while(replica_server[y].ip!=NULL){
        snprintf(msg,MAX_LEN,"\n%d) host:%s, port:%d  \n", (y+1),replica_server[y].ip,replica_server[y].port);
        logger(msg);
        // comunicate to all sync servers
        int sd = connectbyportint(replica_server[y].ip,replica_server[y].port);
        if (sd == err_host) {
            printf("\nCannot find host %s\n", replica_server[y].ip);
            success = 0;
            break;
        }
        if (sd < 0) {
            perror("\nconnectbyport");
            success = 0;
            break;
        }
        // we now have a valid, connected socket
        printf("\nConnected to %s  on port %d\n", replica_server[y].ip, replica_server[y].port);
        // all the sync servers fds are stored in the array
        rep_fds[y] = sd;
        y++;
    }
    char req[MAX_LEN],ans[MAX_LEN];


    // phase one state after establishing of the connection with the clients
    if(success == 1){

        for(int k = 0;k<y;k++){

            // server sends the precommit message and initiate phase 1
            sprintf(req,"PRECOMMIT");
            send(rep_fds[k],req,strlen(req),0);
            send(rep_fds[k],"\n",1,0);
            int n = readline(rep_fds[k],ans,MAX_LEN-1);
            printf("\nresponse : %s, n = %d\n", ans,n);
            if ( n > 1 && ans[n-1] == '\r' ) 
                ans[n-1] = '\0';
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: --> %s\n", __FILE__, ans);
                logger(msg);
            } /* DEBUG_COMM */
                // When server is readhy to commit then it will send READY to sync server

            if ( strncasecmp(ans,"READY",strlen("READY")) == 0 ) {  // we are done!
                snprintf(msg, MAX_LEN, "%s:  received  READY from sync server %d (%s), closing\n", __FILE__, rep_fds[k], replica_server[y].ip);
                logger(msg);
                if (debugs[DEBUG_COMM]) {
                    snprintf(msg, MAX_LEN, "%s: <-- OK 0 nice talking to you\n", __FILE__);
                    logger(msg);
                } /* DEBUG_COMM */
                // send(sd,"OK 0 nice talking to you\r\n", strlen("OK 0 nice talking to you\r\n"),0);

            }else{
                success = 0;
                break;
            }
            

        }

        //completed phase1, initiating phase2
        if(success == 1){
            sprintf(msg,"Successfully compleated phase 1\n now in phase 2");
            logger(msg);
            int k = 0;
            for(k=0;k<y;k++){
                if(reqType==1){

                    // in phase 2, operation is executed... either WRITE or REPLACE
                    sprintf(req,"WRITE %s",input);
                    snprintf(msg, MAX_LEN, "%s: Writing %s to sync server\n", __FILE__,input);
                    logger(msg);
                }else if(reqType==2){
                    sprintf(req,"REPLACE %d %s",msg_id,input); 
                    snprintf(msg, MAX_LEN, "%s: Replicating %d) message with ID %s to sync server\n", __FILE__,msg_id, input);
                    logger(msg);
                    
                }
                send(rep_fds[k],req,strlen(req),0);
                send(rep_fds[k],"\n",1,0);
                // read and display the response (which is exactly one line long):
                int n = readline(rep_fds[k],ans,MAX_LEN-1);
                printf("\nresponse : %s, n = %d\n", ans,n);
                if ( n > 1 && ans[n-1] == '\r' )
                    ans[n-1] = '\0';
                if (debugs[DEBUG_COMM]) {
                    snprintf(msg, MAX_LEN, "%s: --> %s\n", __FILE__, ans);
                    logger(msg);
                } /* DEBUG_COMM */

                    // once we recieve SUCCESS from the server that means other server is completed the work

                if ( strncasecmp(ans,"SUCCESS",strlen("SUCCESS")) == 0 ) {  // we are done!
                    snprintf(msg, MAX_LEN, "%s: received SUCCESS  from sync server %d (%s), closing\n", __FILE__, rep_fds[k], replica_server[y].ip);
                    logger(msg);
                    if (debugs[DEBUG_COMM]) {
                        snprintf(msg, MAX_LEN, "%s: SUCCESS received from sync server, closing\n", __FILE__);
                        logger(msg);
                    }

                }else{
                    success = 0;
                    break;
                }
                

            }

            // completed all actions
            if(success == 1){
                for(int s=0;s<k;s++){
                    close(rep_fds[s]);
                }
                return 3;

            }else{
                // failed in phase 2
                return -4;
            }


        }else{
            // failed in phase 1
            return -3;
        }
        
    }else{
        // failed to establish connections
        return -1;
    }
    return 2;     
}

int write_message(int fdd, const char* input, size_t input_length) {
    int result;
    char msg[MAX_LEN];  // logger string
    int fd = bbfd;
    printf("Message received : \t %s",input);
    if (flocks[fd] == 0)
        return err_nofile;
    // Blocking the thread
    pthread_mutex_lock(&flocks[fd] -> mutex);
    
    while (flocks[fd] -> reads != 0) {
        pthread_cond_wait(&flocks[fd] -> can_write, &flocks[fd] -> mutex);
    }

    // DEBUG_DELAY
    if (debugs[DEBUG_DELAY]) {
        // debug delay check.
        snprintf(msg, MAX_LEN, "BULLETIN BOARD SERVER: %s: delaying write for 5 seconds\n", __FILE__);
        logger(msg);
        sleep(5);
        snprintf(msg, MAX_LEN, "BULLETIN BOARD SERVER: %s:  5 seconds delay ends\n", __FILE__);
        logger(msg);
    }

    char mes[MAX_LEN];
    snprintf(mes, MAX_LEN,"%d/%s\n", message_inc_number,input);
    printf("Inserting: %s for file desp:%d fdd:%d\n",mes ,fd, fdd);

    // checking weather the requst is from client or sync server
    if(fdd==1 || rep_serv_count < 1){
        result = write(fd,mes,strlen(mes));
        if (result == -1) {
            snprintf(msg, MAX_LEN, "%s: write errorsds: %s\n", __FILE__, strerror(errno));
            logger(msg);
        }else{
            message_inc_number++;
        }
    }else if(fdd==0){
        int r = phase_two_commit(1,input,0);
        printf("r value in write fuinction is %d\n",r);
        if( r > 0){
               // printf("hello, i have reached this point");
                result = write(fd,mes,strlen(mes));
                if (result == -1) {
                    snprintf(msg, MAX_LEN, "%s: WRITE cannot be performed due to error: %s\n", __FILE__, strerror(errno));
                    logger(msg);
                }else{
                    message_inc_number++;
                }

        }else{
                snprintf(msg, MAX_LEN, "%s: Failed protocol: %s\n", __FILE__, strerror(errno));
                logger(msg);
                result = r;
            }

    }

    pthread_cond_broadcast(&flocks[fd] -> can_write);
    pthread_mutex_unlock(&flocks[fd] -> mutex);
    return result;
}

int read_message(int mn, char* resStr, size_t input_length,int cs) {
    int result = -4;
    char msg[MAX_LEN];  // logger string
    char input[MAX_LEN];
    int fd = bbfd;
    if (flocks[fd] == 0)
        return err_nofile;

    

    pthread_mutex_lock(&flocks[fd] -> mutex);
    // We increment the number of concurrent reads, so that a write
    // request knows to wait after us.
    flocks[fd] -> reads ++;
    pthread_mutex_unlock(&flocks[fd] -> mutex);

    // now we have the condition set, so we read (we do not need the
    // mutex, concurrent reads are fine):

    if (debugs[DEBUG_DELAY]) {       
        snprintf(msg, MAX_LEN, "%s: Thread delayed for 20 seconds\n", __FILE__);
        logger(msg);
        sleep(20);
        snprintf(msg, MAX_LEN, "%s: Hurrey Delay has been completed.\n", __FILE__);
        logger(msg);
    }


    if (debugs[DEBUG_FILE]) {
        snprintf(msg, MAX_LEN, "%s: read %lu bytes from descriptor %d\n", __FILE__, input_length, fd);
        logger(msg);
    }

    //we need fectch all reords from the bbfile

    lseek(fd,0,SEEK_SET);

    while(readline(fd, input , input_length) >0){
            printf("Content (%d): %s\n",cs,input);
            int len = strlen(input);
            char *data_ptr = strtok(input, "/");
            printf("str : %s,value :%d \n", data_ptr,atoi(data_ptr));
            int msgnm = atoi(data_ptr);
            char intr_str[MAX_LEN];
            if(msgnm == mn){
                data_ptr = strtok(NULL, "/");
                strcpy(intr_str, data_ptr);
                if(data_ptr !=NULL){
                    data_ptr = strtok(NULL, "/");
                    strcat(intr_str, "/");
                    strcat(intr_str, data_ptr);
                    if(data_ptr !=NULL){                        
                        strcpy(resStr,intr_str);
                        printf("Message to be sent to the client: %s\n\n",resStr);
                        int curr_pos = lseek(fd, 0, SEEK_CUR);
                        printf("Found client expected message : %d position, on line %d",curr_pos,(curr_pos-len));
                        result = strlen(intr_str);
                        break;
                    }else{
                        // Message not present
                        printf("Message unavailable.\n");

                    }
                }else{
                    // poster not present
                    printf("poster unavailable.");

                }
                
            }
                
    }


    pthread_mutex_lock(&flocks[fd] -> mutex);
    flocks[fd] -> reads --;
    // this might have released the file for write access, so we
    // broadcast the condition to unlock whatever writing process
    // happens to wait after us.
    if (flocks[fd] -> reads == 0)
        pthread_cond_broadcast(&flocks[fd] -> can_write);
    pthread_mutex_unlock(&flocks[fd] -> mutex);
    return result;
}

int replace_message(int message_number, char* replaceStr,char* client_name, size_t input_length,int op) {
    int result=0;
    char msg[MAX_LEN];  // logger string
    char input[MAX_LEN];
    int fd = bbfd;
    char new_text[MAX_LEN] ;

    if (flocks[fd] == 0)
        return err_nofile;
    if (debugs[DEBUG_FILE]) {
        snprintf(msg, MAX_LEN, "%s: Read %lu bytes from descriptor %d\n", __FILE__, input_length, fd);
        logger(msg);
    }
    while (flocks[fd] -> reads != 0) {
        // release the mutex while waiting...
        pthread_cond_wait(&flocks[fd] -> can_write, &flocks[fd] -> mutex);
    }

    if (debugs[DEBUG_DELAY]) {
        // ******************** TEST CODE ******************** 
        // bring the read process to a crawl to figure out whether we
        // implement the concurrent access correctly.
        snprintf(msg, MAX_LEN, "%s: Thread delayed for 20 seconds\n", __FILE__);
        logger(msg);
        sleep(20);
        snprintf(msg, MAX_LEN, "%s: Hurrey Delay has been completed.\n", __FILE__);
        logger(msg);
        // ******************** TEST CODE DONE *************** 
    } /* DEBUG_DELAY */



    //we need fectch all reords from the bbfile

    lseek(fd,0,SEEK_SET);
    int i=0;
    int found = 0;
    char text_torep[MAX_LEN];

    while(readline(fd, input , MAX_LEN) >0){
            i++;
            // int len = strlen(input);
            char *data_ptr = strtok(input, "/");
            printf("str : %s,value :%d \n", data_ptr,atoi(data_ptr));
            int msgnm = atoi(data_ptr);
            data_ptr = strtok(NULL, "/");
            data_ptr = strtok(NULL, "/");
            if(msgnm == message_number){
                
                printf("Found client expected message : %d/%s at line : %d\n",msgnm,strtok(input, "/"),i);
                sprintf(new_text,"%d/%s/%s\n",msgnm,client_name,replaceStr);
                sprintf(text_torep,"%s/%s\n",client_name,replaceStr);
                printf("Text to be replaced : %s\n test to sync : %s",new_text,text_torep);
                found =1;
                break;
                
            }
    }

    if(found == 1){

        int rep = 0;


        if(op==1 || rep_serv_count < 1){
            rep =1;
        }else if(op==0){
            int r = phase_two_commit(2,text_torep,message_number);
            if( r > 0){
                    rep =1;

            }else{
                    snprintf(msg, MAX_LEN, "%s: Two phase commit protocol has failed : %s\n", __FILE__, strerror(errno));
                    logger(msg);
                    result = r;
                }

        }
        if(rep==1){

            // replace here 
            
            sprintf(msg,"creating temp\n");
            logger(msg);
            const char* tempfile = "reptemp";
            const char* bbfile = "bbfile.txt";
            int temf = open(tempfile, O_RDWR| O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            if(temf<0){
                perror("REPLACE cannot be performed at the moment");
                sprintf(msg,"REPLACE res 2 cannot be performed\n");
                logger(msg);
                result = -3;
            }else{
                int curline =0;
                char file_content[MAX_LEN];
                lseek(fd,0,SEEK_SET);
                while(readline(fd, file_content , MAX_LEN) >0){
                    strcat(file_content,"\n");
                    printf("Content (%d): %s\n",curline,file_content);

                    // file_content[strlen(file_content)] = '\n';
                    // file_content[strlen(file_content)+1] = '\0';
                    curline++;
                    if(curline == i){
                        result = write(temf,new_text,strlen(new_text));
                        if (result == -1) {
                            snprintf(msg, MAX_LEN, "%s: WRITE errorsds: %s\n", __FILE__, strerror(errno));
                            logger(msg);
                            result = -5;
                            break;
                        }
                    }else{
                        result = write(temf,file_content,strlen(file_content));
                        if (result == -1) {
                            snprintf(msg, MAX_LEN, "%s: WRITE errorsds: %s\n", __FILE__, strerror(errno));
                            logger(msg);
                            result = -6;
                            break;
                        }
                    }
                }
                close(bbfd);
                close(temf);
                remove(bbfile);
                rename(tempfile, bbfile);
                bbfd = open(bbfile, O_RDWR|O_APPEND, S_IRUSR | S_IWUSR);
                if (bbfd < 0) {
                    perror("bbfd file after performing replace.\n");
                    printf("Unable to write PID.\n");
                    result= -100;


                }else{
                    result = 1;
                }
            }

        }

    }else{
        result = -3;
    }

    // we are done with the file, we first signal the condition
    // variable and then we return.

   
    pthread_cond_broadcast(&flocks[fd] -> can_write);
    pthread_mutex_unlock(&flocks[fd] -> mutex);
    return result;
}




/*
 * Client handler for the file server.  Receives the descriptor of the
 * communication socket.
 */
void* bulletin_board_server_client (client_t* clnt) {


    printf("BULLETIN BOARD SERVER: started with fd: %d\n",bbfd);
    strcpy(clnt->c_name, "nobody");

    
    char* ip = clnt -> ip;

    char req[MAX_LEN];  // current request
    char msg[MAX_LEN];  // logger string
    int n;
    if(open_fds==NULL){
        open_fds = new bool[flock_size];
        for (size_t i = 0; i < flock_size; i++)
            open_fds[i] = false;
    }

    // make sure req is null-terminated...
    req[MAX_LEN-1] = '\0';

    snprintf(msg, MAX_LEN, "Current thread: %d \n",clnt->status);
    logger(msg);

    for(int y =0 ;y<rep_serv_count;y++){
        snprintf(msg,MAX_LEN,"REPLICA SERVER: %d ) %s host %d  \n", (y+1),replica_server[y].ip,replica_server[y].port);
        logger(msg);
    }

    pthread_mutex_lock(&(clnt->lock));


    pthread_cond_wait(&(clnt -> thread_cond),&(clnt->lock));

    int sd = clnt -> sd;

    snprintf(msg, MAX_LEN, "BULLETIN BOARD SERVER: %s: new client from %s assigned socket descriptor %d\n",
             __FILE__, ip, sd);
    logger(msg);
    snprintf(msg, MAX_LEN,
             "Welcome to Akshay's server v.1 [%s]. Here are the list of commands which can be passed to the server: \n USER <name> name should not contain '/'\n READ <message-number> \n WRITE <message> \n REPLACE <message-number>/<message> \n QUIT <text>.\r\n", ip);
    send(sd, msg, strlen(msg), 0);

    // Loop while the client has something to say...
    while ((n = readline(sd,req,MAX_LEN-1)) != recv_nodata) {
        char* ans = new char[MAX_LEN]; // the response sent back to the client
        
        ans[MAX_LEN-1] = '\0';

        if ( n > 1 && req[n-1] == '\r' )
            req[n-1] = '\0';
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: --> %s ; len : %lu\n", __FILE__, req,strlen(req));
            logger(msg);
        } /* DEBUG_COMM */
        if ( strncasecmp(req,"QUIT",strlen("QUIT")) == 0 ) {  // we are done!
            snprintf(msg, MAX_LEN, "%s: BULLETIN BOARD SERVER: Received QUIT from client %d (%s), So closing the connection.\n", __FILE__, sd, ip);
            logger(msg);
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: <-- Bye Have a nice day.... \u263A\n", __FILE__);
                logger(msg);
            } /* DEBUG_COMM */
            send(sd,"Bye Have a nice day.... \u263A\r\n", strlen("Bye Have a nice day.... \u263A\r\n"),0);
            shutdown(sd, SHUT_RDWR);
            close(sd);
            delete[] ans;
            delete clnt;
            return 0;
        }
        // ### COMMAND HANDLER ###


        // ### READ ###
        else if (strncasecmp(req,"READ",strlen("READ")) == 0 ) {
            int idx = next_arg(req,' ');
            if (idx == -1)
                snprintf(ans,MAX_LEN,"ERROR READ %d requires a Message Number NONE given.\n", EBADMSG);
            else {
                    if (debugs[DEBUG_COMM]) {
                        snprintf(msg, MAX_LEN, "%s: (before decoding) will read message '%s' from Bulletin Board File.\n",
                                 __FILE__, &req[idx]); 
                        logger(msg);
                    }
                    idx = atoi(&req[idx]);  // get the identifier and length
                    if (debugs[DEBUG_COMM]) {
                        snprintf(msg, MAX_LEN, "%s: (after decoding) will read message '%d' from Bulletin Board File.\n",
                                 __FILE__, idx); 
                        logger(msg);
                    }
                    if (idx <= 0)
                        snprintf(ans, MAX_LEN,
                                 "ERROR READ Only positive numbers are accepted.");
                    else { // now we can finally read the thing!
                        // read buffer
                        char* read_buff = new char[100];
                        int result = read_message(idx, read_buff, 100,sd);
                        // ASSUMPTION: we never read null bytes from the file.
                        if (result == err_nofile) {
                            snprintf(ans, MAX_LEN, "ERROR READ Bulletin Board File");
                        }
                        else if (result < 0) {
                            snprintf(ans, MAX_LEN, "UNKNOWN %d Could not find the message number. Please give a valid one.", idx);
                        }
                        else {
                            printf("result : %d, str :%s\n\n",result,read_buff);
                            read_buff[result] = '\0';
                            // we may need to allocate a larger buffer
                            // besides the message, we give 40 characters to OK + number of bytes read.
                            delete[] ans;
                            ans = new char[40 + result];
                            snprintf(ans, MAX_LEN, "MESSAGE %d %s", idx, read_buff);
                        }
                        delete [] read_buff;
                    }               
            }
        } // end READ

        // ### WRITE ###
        else if (strncasecmp(req,"WRITE",strlen("WRITE")) == 0 ) {
            int idx = next_arg(req,' ');
            if (idx == -1 ) {
                snprintf(ans,MAX_LEN,"BULLETIN BOARD SERVER: FAIL %d WRITE requires a message", EBADMSG);
            }
            else { // we attempt to open the file
                char new_message[MAX_LEN];
                // do we have a relative path?
                // awkward test, do we have anything better?
                if (req[idx] == '/') { // absolute
                    snprintf(new_message, MAX_LEN, "%s", &req[idx]);
                }
                else { // relative
                    snprintf(new_message, MAX_LEN, "%s/%s", clnt->c_name, &req[idx]);
                    if (debugs[DEBUG_FILE]) {
                            snprintf(msg, MAX_LEN, "%s: Writing Message %s\n", __FILE__, new_message);
                            logger(msg);
                    }
                    int result = write_message(0, new_message, strlen(new_message));
                    if (result == err_nofile)
                        snprintf(ans, MAX_LEN, "BULLETIN BOARD SERVER: FAIL %d bad file descriptor %d", EBADF, idx);
                    else if (result < 0) {

                        switch(result){


                            case -4: snprintf(ans, MAX_LEN, "ERROR WRITE protocol phase 2 has been failed "); break;
                            case -3: snprintf(ans, MAX_LEN, "ERROR WRITE protocol phase 1 has been failed"); break;
                            case -1: snprintf(ans, MAX_LEN, "ERROR WRITE Failed to establish connections"); break;
                            default : snprintf(ans, MAX_LEN, "ERROR WRITE"); break;
                        }




                        snprintf(ans, MAX_LEN, "ERROR WRITE");
                    }
                    else {
                            snprintf(ans, MAX_LEN, "WROTE %d", (message_inc_number-1));
                    }
                }                
            }
        } // end WRITE

        // ### USER ###
        else if (strncasecmp(req,"USER",strlen("USER")) == 0 ) {
            int idx = next_arg(req,' ');
            if (idx == -1 ) {
                snprintf(ans,MAX_LEN,"FAIL %d USER requires a poster", EBADMSG);
            }
            else { // we attempt to open the file
                char new_message[MAX_LEN];
                // do we have a relative path?
                // awkward test, do we have anything better?
                if (req[idx] == '/') { // absolute
                    snprintf(new_message, MAX_LEN, "%s", &req[idx]);
                }
                else { // relative
                    snprintf(clnt->c_name, MAX_LEN, "%s", &req[idx]);
                    snprintf(ans, MAX_LEN, "1.0 HELLO %s", clnt->c_name);

                }                
            }
        } // end USER




        // ### REPLACE ###
        else if (strncasecmp(req,"REPLACE",strlen("REPLACE")) == 0 ) {
            printf("Request from the client: %s", req);
            int idx = next_arg(req,' ');
            if (idx == -1) // no argument!
                snprintf(ans,MAX_LEN,"ERROR %d REPLACE required a Message Number. Please follow this format: REPLACE <message-number>/<message>", EBADMSG);
            else {
                int idx1 = next_arg(&req[idx],'/');
                if (idx1 == -1) // no data to write
                    snprintf(ans,MAX_LEN,"ERROR %d REPLACE requires message to be over-write. Please follow this format: REPLACE <message-number>/<message>", EBADMSG);
                else {
                    idx1 = idx1 + idx;
                    req[idx1 - 1] = '\0';
                    idx = atoi(&req[idx]);  // get the identifier and data
                    if (idx <= 0)
                        snprintf(ans,MAX_LEN,
                                 "ERROR %d message number must be positive. Please follow this format: REPLACE <message-number>/<message>", EBADMSG);
                    else { // now we can finally write!
                        if (debugs[DEBUG_FILE]) {
                            snprintf(msg, MAX_LEN, "%s: will write (%s)\n", __FILE__, &req[idx1]);
                            logger(msg);
                        }
                        int result = replace_message(idx, &req[idx1],clnt->c_name, strlen(&req[idx1]),0);
                        if (result == err_nofile)
                            snprintf(ans, MAX_LEN, "ERROR %d bad file descriptor %d", EBADF, idx);
                        else if (result < 0) {
                            snprintf(ans, MAX_LEN, "UNKNOWN %d", idx);
                        }
                        else {
                            snprintf(ans, MAX_LEN, "REPLACED %d", idx);
                        }
                    }
                }
            }
        }
        // ### end REPLACE ###
    

        // ### UNKNOWN COMMAND ###
        else {
            int idx = next_arg(req,' ');
            if ( idx == 0 )
                idx = next_arg(req,' ');
            if (idx != -1)
                req[idx-1] = '\0';
            snprintf(ans,MAX_LEN,"ERROR %d %s not understood", EBADMSG, req);
        }

        // ### END OF COMMAND HANDLER ###

        // Effectively send the answer back to the client:
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
            logger(msg);
        } /* DEBUG_COMM */
        send(sd,ans,strlen(ans),0);
        send(sd,"\r\n",2,0);        // telnet expects \r\n
        delete[] ans;

        if(clnt -> shutdown ==1){
            close(sd);
            // delete[] open_fds;
            exit(0);
        }


    } // end of main loop.
    pthread_mutex_unlock(&(clnt->lock));

    // read 0 bytes = EOF:
    snprintf(msg, MAX_LEN, "%s: client with descriptor %d (%s) left, so closing connection.\n",
             __FILE__, sd, ip);
    logger(msg);
    shutdown(sd, SHUT_RDWR);
    close(sd);
    clnt->status = recreate;  
    // delete[] open_fds;
    pthread_cancel(pthread_self());
    // delete clnt;
    return 0;
}



void* sync_server_client (client_t* clnt) {
    int sd = clnt -> sd;
    char* ip = clnt -> ip;


    // default cliet name is set to nobody
    strcpy(clnt->c_name,"nobody");

    char req[MAX_LEN];  // current request
    char msg[MAX_LEN];  // logger string
    int n;
    bool* open_fds = new bool[flock_size];
    for (size_t i = 0; i < flock_size; i++)
        open_fds[i] = false;

    // make sure req is null-terminated...
    req[MAX_LEN-1] = '\0';

    snprintf(msg, MAX_LEN, "%s: new client from %s came with socket descriptor %d\n",
             __FILE__, ip, sd);
    logger(msg);
    snprintf(msg, MAX_LEN, "Initiating Two-Phase Commit protocol. \r\n");

    while ((n = readline(sd,req,MAX_LEN-1)) != recv_nodata) {
        char* ans = new char[MAX_LEN]; 
        ans[MAX_LEN-1] = '\0';
        if ( n > 1 && req[n-1] == '\r' ) 
            req[n-1] = '\0';
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: --> %s\n", __FILE__, req);
            logger(msg);
        } 
        if ( strncasecmp(req,"QUIT",strlen("QUIT")) == 0 ) { 
            snprintf(msg, MAX_LEN, "%s: QUIT received from client %d (%s), closing\n", __FILE__, sd, ip);
            logger(msg);
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: <-- OK 0 nice talking to you\n", __FILE__);
                logger(msg);
            } /* DEBUG_COMM */
            send(sd,"OK 0 nice talking to you\r\n", strlen("OK 0 nice talking to you\r\n"),0);
            shutdown(sd, SHUT_RDWR);
            close(sd);
            delete[] ans;
            delete[] open_fds;
            delete clnt;
            return 0;
        }
        // ### PRE-COMMIT ###
        else if (strncasecmp(req,"PRECOMMIT",strlen("PRECOMMIT")) == 0 ) {
            // once server recieves pre-commit, then the response as ready is sent to the sync server
            snprintf(ans, MAX_LEN, "READY");
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                logger(msg);
            } 
            send(sd,ans,strlen(ans),0);
            send(sd,"\r\n",2,0);
            delete[] ans;

            // after completing phase -1 server goes to phase 2, where Write , REPALCE and ABORT commands are handled

            while ((n = readline(sd,req,MAX_LEN-1)) != recv_nodata) {

                snprintf(ans, MAX_LEN, "Inside inner loop");
                if (debugs[DEBUG_COMM]) {
                    snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                    logger(msg);
                } 
                delete[] ans;
                if (strncasecmp(req,"ABORT",strlen("ABORT")) == 0 ) {
                    snprintf(ans, MAX_LEN, "ABORTED");


                } // end WRITE

                // ### WRITE ###
                else if (strncasecmp(req,"WRITE",strlen("WRITE")) == 0 ) {
                    int idx = next_arg(req,' ');
                    if (idx == -1 ) {
                        snprintf(ans,MAX_LEN,"FAIL");
                    }
                    else { 
                        char new_message[MAX_LEN];
                        if (req[idx] == '/') { 
                            snprintf(new_message, MAX_LEN, "%s", &req[idx]);
                        }
                        else {
                            snprintf(new_message, MAX_LEN, "%s", &req[idx]);
                            if (debugs[DEBUG_FILE]) {
                                    snprintf(msg, MAX_LEN, "%s: will write %s\n", __FILE__, new_message);
                                    logger(msg);
                            }
                            int result = write_message(1, new_message, strlen(new_message));
                            if (result == err_nofile)
                                snprintf(ans, MAX_LEN, "FAIL");
                            else if (result < 0) {
                                snprintf(ans, MAX_LEN, "FAIL");
                            }
                            else {
                                    snprintf(ans, MAX_LEN, "SUCCESS");
                            }
                        }                
                    }
                } // end WRITE
                // ### REPLACE ###
                else if (strncasecmp(req,"REPLACE",strlen("REPLACE")) == 0 ) {
                    int idx = next_arg(req,' ');
                    if (idx == -1) // no argument!
                        snprintf(ans,MAX_LEN,"FAIL ");
                    else {
                        int idx1 = next_arg(&req[idx],' ');
                        if (idx1 == -1) // no data to write
                            snprintf(ans,MAX_LEN,"FAIL");
                        else {
                            idx1 = idx1 + idx;
                            req[idx1 - 1] = '\0';
                            idx = atoi(&req[idx]);  // get the identifier and data
                            if (idx <= 0)
                                snprintf(ans,MAX_LEN,
                                         "FAIL");
                            else { // now we can finally write!
                                if (debugs[DEBUG_FILE]) {
                                    snprintf(msg, MAX_LEN, "%s: will write %s\n", __FILE__, &req[idx1]);
                                    logger(msg);
                                }
                                int result = replace_message(idx, &req[idx1],clnt->c_name, strlen(&req[idx1]),1);
                                if (result == err_nofile)
                                    snprintf(ans, MAX_LEN, "FAIL");
                                else if (result < 0) {
                                    snprintf(ans, MAX_LEN, "FAIL ");
                                }
                                else {
                                    snprintf(ans, MAX_LEN, "SUCCESS");
                                }
                            }
                        }
                    }
                }else{
                    snprintf(ans, MAX_LEN, "Terminating the process due to UNKNOWN command. ");
                    if (debugs[DEBUG_COMM]) {
                        snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                        logger(msg);
                    } /* DEBUG_COMM */
                    send(sd,ans,strlen(ans),0);
                    send(sd,"\r\n",2,0);
                    break;

                }
                // Effectively send the answer back to the client:
                if (debugs[DEBUG_COMM]) {
                    snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                    logger(msg);
                } /* DEBUG_COMM */
                send(sd,ans,strlen(ans),0);
                send(sd,"\r\n",2,0);        // telnet expects \r\n
                break;
            }

        }
        // ### end REPLACE ###
        
        // ### UNKNOWN COMMAND ###
        else {
            int idx = next_arg(req,' ');
            if ( idx == 0 )
                idx = next_arg(req,' ');
            if (idx != -1)
                req[idx-1] = '\0';
            snprintf(ans,MAX_LEN,"ERROR Cannot understand %d %s ", EBADMSG, req);
             // Effectively send the answer back to the client:
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                logger(msg);
            } /* DEBUG_COMM */
            send(sd,ans,strlen(ans),0);
            send(sd,"\r\n",2,0);        // telnet expects \r\n

            delete[] ans;
            }

        // ### END OF COMMAND HANDLER ###

       
    } // end of main loop.

    // read 0 bytes = EOF:
    snprintf(msg, MAX_LEN, "SYNC SERVER %s with socket descriptor %d (%s) left, so closing the it.\n",
             __FILE__, sd, ip);
    logger(msg);
    shutdown(sd, SHUT_RDWR);
    close(sd);
    delete clnt;
    return 0;
}
