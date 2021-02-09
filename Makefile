CXX = g++
CXXFLAGS = -g -Wall -Werror -ansi -pedantic
LDFLAGS = $(CXXFLAGS) -pthread

all: server

tcp-con.o: tcp-con.h tcp-con.cc
	$(CXX) $(CXXFLAGS) -c -o tcp-con.o tcp-con.cc
tokenize.o: tokenize.h
	$(CXX) $(CXXFLAGS) -c -o tokenize.o tokenize.cc

server.o: tcp-con.h server.h server.cc
	$(CXX) $(CXXFLAGS) -c -o server.o server.cc

utils.o: tcp-con.h server.h utils.cc
	$(CXX) $(CXXFLAGS) -c -o utils.o utils.cc

server: tokenize.o tcp-con.o server.o utils.o
	$(CXX) $(LDFLAGS) -o server tokenize.o tcp-con.o server.o utils.o 

clean:
	rm -f *~ *.o *.bak core \#*

