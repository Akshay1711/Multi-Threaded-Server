# Multi Threaded Server in C
The server is implemented in the files server.h (header for everything),
server.cc (code for the file server), utils.cc (common code and main functions) 
as a no-frills, multithreaded server.  There are two other support files named 
tokenize.h(Used for string tokenizing functions) and tcp_con.h (Provides TCP 
protocol functions) which declares the functions and the corresponding code 
is present in tokenize.cc and tcp_con.cc. It also detaches by default from the 
terminal and redirects output to the file "shfd.log" in the current working
directory.



## 1. User guide:
The following command line is accepted:

  `./server [-d] [-D] [-b <bbfile name>] [-p <file server port>] [-T <Max thread pool size>] [-s <replication server port>] <IP address:port> [list Sync servers]`

where

* -b overrides (or sets) the Bulletin Board server file name as per the given argument.
* -T overrides T max value as per the given argument.
* -p overrides the port number bp as per the given argument.
* -s overrides the port number sp as per the given argument.
* -f (with no argument) forces d(DAEMON) to false or 0.
* -d (with no argument) forces D(DEBUG) to true or 1.

This server works for any kind of client, including telnet. The
problem with the telnet is that it sends `\r\n`-terminated requests
instead of the Unix-like \n-terminated requests.  For compatibility
with various clients we have designed the server in such a way that 
the server will also accept \n-terminated requests. So this server
accepts _both_ formats (specifically, it removes the terminating \r if
present), and all the responses given are \r\n-terminated.


## 2. Supporting files:
- Module tcp-con, which consists of all the generic methods required for TCP communication.

- Module tokenize, has the generic functions required to perform string tokenization.

- The makefile, whose target 'all' prepares the server,

- server.cc is the file where all the server functionalities are implemented.

- utils.cc consists of the main function and initilization part of the servers.


## 3. Implementation details
The server implementation has following parts namely Bulletin Board Server, a Replication server, Signal Handling.

## 3.1. Bulletin Board Server
This is a simple file server which accepts 5 commands USER, READ, WRITE, REPLACE 
and QUIT and performs the corresponding actions to the bbserver file(The file 
name might not be the same as it changes as per the input given while running the 
server) present in the server. The commands work as follows:

* USER - This command is recognize the user and stores its name in the server which
will be used in futher commands.
* READ - Reads specfic message number given by the client and sends it's poster/message 
as response.
* WRITE - It writes the data sent by the client with username as poster/ the message sent
by the client.
* REPLACE - It replaces the message number sent by the client and responds accordingly.
* QUIT - It stops the client interaction and closes the socket.  

#### Note: Bulletin Board server commands are case insensitive and everything other than that are case sensitive.

## 3.2. Thread management in File server
The maximum number of threads that are created to serve the clients is referred as `t_max`. Rather than creating new threads we are managing the clients by reallocating the same thread which was used by another client. The whole process goes as follows:
- When the server starts there will be a set of pre-allocated threads to 
serve the clients(t_max).

- When a new client hits the server one of the pre-allocated threads will be
assigned to them for their use.

- In a best case if the client leaves after fewer requests, then the next 
client will be assigned with the same thread used by the previous client.

- In the worst case if all t_max threads are busy serving clients then the 
server responds to the client that it will not accept any more clients.


## 3.3. System Signal Handling(SIGQUIT & SIGHUP)
- When `SIGQUIT` signal is triggered by the system or explictly, the server will 
first wait for working threads to complete the work and then exit all the 
threads, close the sockets and then terminates the program. 

- When `SIGHUP` signal is triggered explictly or by the system the server will 
first wait for working threads to complete their work and then exits all the
threads, closes all the sockets and then restarts the program.

## 3.4 Syncronization
This is the part where multiple peer servers will be created which contains all the functionalities mentioned above. Syncronization in this server is performed by using peer creation, communication and master slave architecture to maintain all the peers in sync.

The syncronization is performed only for `WRITE` and `REPLACE` commands.Peers are the primary requirements to perform syncronization, there should be atleast one peer 
running to perform syncronization. Once the server and its peers are up and running the master slave architecture comes into picture, which will be implemented as follows:
1. The client that requests for either a `WRITE` or `REPLACE` operation is considered as
master and all its peers are considered as slave.
2. When the server handles `WRITE` or `REPLACE` request it first checks whether all its peers are ready to perform the corresponding operation. For this a `PRECOMMIT` command is sent to the peers, if atleast one of the peers either sends an error response or doesn't send the response to the server then the corresponding `WRITE` or `REPLACE` operation will be cancelled in both server and peers and an appropriate error message is sent to client by the server.
3. If the `PRECOMMIT` broadcasted from the server gets a `SUCCESS` response then the nextphase begins which is to perform the corresponding `WRITE` or `REPLACE` operation. This is performed using the `COMMIT` command.
4. Once the `COMMIT` is broadcasted to all the peers, the peers will perform the 
corresponding `WRITE` or `REPLACE` operation and once they give a `SUCCESS` response the server performs the operation to the file and responds to the client accrodingly.
5. If any of the peers sends either an error message or if there is no response until timeout then the server sends abort to all its peers and the operation is aborted by the server and a corresponding message is sent to the client accordingly.
6. By following the above process all the master and slave servers will be in sync and 
hence attains syncronization.


## 4. Tests
- All the best cases in which there is highest possibility of pass are tested 
accross multiple clients and servers.

- The commands `QUIT`, `USER`, `READ`, `WRITE`, `REPLACE` are tested in all the phases of the application protocol and they work fine.

- Boundary conditions like access to the server when `t_max` client are already working on the server are tested.

- Tested the syncronization between two servers by writing or replacing a message and one server and reading in another vice versa.

- Two phase protocol has precommit phase and commit phase, where in precommit phase server send `PRECOMMIT` message, as soon as the sync server recieves `PRECOMMIT` if server is ready it replies with `READY` upon reciveing `READY` signal from all the servers, then application is taken to the second phase if now OPERATION is failed.

- Once the server reaches to the phase two, then server sends either `WRITE` or `REPLACE` command to the server. Then the server is handled the respective command and replies with `SUCCESS`. After recieving the `SUCCESS` from the servers then the file is modified in the local server.
