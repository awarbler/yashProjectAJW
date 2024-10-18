# Compiler to be used for compiling the code 
CC = gcc

# compiler flags -Wall: enanbles all compilers warning messages
# pthread links the pthread library for multithreading support 
CFLAGS = -Wall -pthread

# target to build the server (yashd) and the client(yash)
TARGETS = yashd yash yashTestd

# default target that will be built if 'make' is run without arguments
all: $(TARGETS) # BUILD BOTH YASHD AND YASH executables


# RULE to build the server executable(yashd)
yashd: yashd.o yashTestd.o
	$(CC) $(CFLAGS) yashd.o yashTestd.o -o yashd 
# compile and link the yashd object file to create the executable yashd

# Rule to buile the client executable (yash)
# second line compiles and link the yash object file to create the executable yash
yash: yash.o
	$(CC) $(CFLAGS) yash.o -o yash

# Compile the yashd source file into an object file 
# make sure obj directory exisit
# compile yash.c into yash.o, include the headers from the include directory
yashd.o: yashd.c yashd.h
	$(CC) $(CFLAGS) -c yashd.c -o yashd.o 

# Compile the yash source file into an object file 
# compile yash.c into yash.o, include the headers from the included directory
yash.o: yash.c yashd.h 
	$(CC) $(CFLAGS) -c yash.c -o yash.o 

yashTestd.o: yashTestd.c yashTestd.h 
	$(CC) $(CFLAGS) -c yashTestd.c -o yashTestd.o 
 
# clean up rule to remove compiled object files and executables 
clean:
	@echo "cleaning up object files and executables....."
	rm -f *.o yashd yash mock_server test_yash pipes yashTestd



