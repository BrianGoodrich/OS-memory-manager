#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <string.h>
#include <stdbool.h>


//Shared memory for clock
#define CLOCK_SHMKEY 511598

#define CLOCK_BUF (sizeof(int) * 2)

//Permissions for msg queue
#define PERMS 0644

//Struct for the queue so that we can have some data for each item in queue.
struct blockedProc{
int process;
int timeBlocked;
int request;
int operation;
};

//Vars for q
struct blockedProc procArray[18];
int front = 0;
int rear = -1;
int itemCount = 0;

//Struct for frame table so that we can keep track of if a frame is occupied, dirty bit set. 
struct frame{
int occupied;
int dirty;
int process;
int page;
};

//msgbuf for messages
struct msgbuf{
long mtype;
char mtext[15];
int request;
int operation; //1 for read 0 for write.
int procIndex;
};

pid_t childPids[18];

//Queue functions
static void myhandler(int s);
bool isEmpty();
bool isFull();
int size();
void enqueue(struct blockedProc);
struct blockedProc dequeue();

//Main
int main (int argc, char** argv){

int i;
for(i = 0; i < 18; i++){
childPids[i] = -2;
}

//Initialize the procArray to default everything to -1.
for(i = 0; i < 18; i++){
procArray[i].process = -1;
procArray[i].timeBlocked = -1;
procArray[i].request = -1;
procArray[i].operation = -1;
}

//Set up frame table
struct frame frameTable[256];
//Default occupied to 0 and dirty/process to -1.
for(i = 0; i < 256; i++){
frameTable[i].occupied = 0;
frameTable[i].dirty = -1;
frameTable[i].process = -1;
frameTable[i].page = -1;
}

//Array of integers, each index is a process and 1 or 0 means that processes are running or not running.
int procs[18];

//Default all procs to 0 for not running;
for(i = 0; i < 18; i++){
procs[i] = 0;
}

//Array of arrays that symbolizes our page table, we have 18 possible processes, and 32 spaces for each page. The value for each page of a process will be the pages index in the frame table if it is there.
int pageTable[18][32];

//Default everything in pageTable to -1
int j;
for(i = 0; i < 18; i++){
	for(j = 0; j < 32; j++){
		pageTable[i][j] = -1;
	}
}

int totalProcesses = 0;
int currentProcs = 0;
int nextSecond = 0;

//Vars for message q
int msqid;
int len;
struct msgbuf buf;
buf.mtype = 1;
strcpy(buf.mtext, "hello\0");
key_t key;
system("touch msgq.txt");

//Set up message queue
if((key = ftok("msgq.txt", 'b')) == -1){
	perror("ftok");
	myhandler(1);
}

if((msqid = msgget(key, PERMS | IPC_CREAT)) == -1){
	perror("msgget");
	myhandler(1);
}


//Set 2 second timer
alarm(2);

//Handle the sigalarm
signal(SIGALRM, myhandler);
signal(SIGINT, myhandler);

//Set up file stuff
int fileLines = 0;
FILE *filePtr = fopen("output.txt", "w");
if(filePtr == NULL){
perror("file open error");
}

//Attach to shared memory for clock
int shmid = shmget(CLOCK_SHMKEY, CLOCK_BUF, 0777 | IPC_CREAT);

if(shmid == -1){
	perror("error in oss shmget for clock");
	myhandler(1);
}

char* paddr = (char*)(shmat(shmid, 0, 0));
int* clock = (int*)(paddr);

clock[0] = 0;
clock[1] = 0;

int x;
int nextProcessIndex = 0;
int flag = 1;
int previousProc = 100;
int frameFlag = 0;
int replaceFrame = 0;

srand(time(NULL));

//Loop looking for memory requests.
while(1){

	//If we pass 1 billion nanoseconds then increment seconds and roll over nanoseconds.
	if(clock[1] >= 1000000000){
		clock[0] += 1;
		clock[1] = clock[1] - 1000000000;
		
		if(fileLines < 10000){
		fprintf(filePtr, "\nCurrent memory layout at TIME: %d sec %d ns\n", clock[0], clock[1]);
		fileLines += 2;
		}

		//Every second output the frame table
		for(i = 0; i < 256; i++){
			if(fileLines < 10000){
			fprintf(filePtr, "          Occupied  DirtyBit  Process  Page\n");
			fprintf(filePtr, "Frame %d:   %d        %d        %d       %d\n", i, frameTable[i].occupied, frameTable[i].dirty, frameTable[i].process, frameTable[i].page);
			fileLines += 2;	
			}
		}
		
	}
	
	//If we have reached 100 total processes terminate
	if(totalProcesses >= 100){
		myhandler(1);
		break;
	}

	//Generate random interval that we will create a process at.
	int newProcInterval = (rand() % 500000000 + 1);

	
if(clock[0] == nextSecond && clock[1] >= newProcInterval && totalProcesses < 18){
		
		//If there are 18 or more total procs, then we need to check and see if they are all running, if they are we wait, if not we assign that index to the next process we generate.
		if(totalProcesses >= 18){
	
			int x;
			//Set nextIndex to a large flag.
			nextProcessIndex = 50;
			for(x = 0; x < 18; x++){
				if(procs[x] == 0 ){
					nextProcessIndex = x;
					flag = 1;
					break;
				}
			}
			//If we have gone through the entire loop above and nextProcessIndex was never set then they are all running, so we wait, set flag.
			if(nextProcessIndex == 50){
				flag = 0;
			}
		}
//If the flag is 1 that means we have an index available and we will generate a new process.
	if(flag == 1){
		if(fileLines < 10000){
			fprintf(filePtr, "Total processes currently = %d launching new process. TIME: %d sec %d ns\n", totalProcesses, clock[0], clock[1]);
			fileLines++;
		}
	
		char charIndex[1];
		
		procs[nextProcessIndex] = 1; //Set this so that now this process is set to running with value of 1.		
		
		sprintf(charIndex, "%d", nextProcessIndex);

		pid_t childPid = fork();
		
		childPids[nextProcessIndex] = childPid;		
		
		if(childPid == -1){
        	perror("failed to fork in oss");
        	myhandler(1);
		}

		if(childPid == 0){
        		char* args[] = {"./user", charIndex, 0};
        		execvp(args[0], args);
		}

		totalProcesses++;
		nextProcessIndex++;
		nextSecond++;
		currentProcs++;
	}	
}


//Now that we have potentially created either our first process or a new process we want to do a msgrcv so that we wait to get a request from one of these processes. Only do this if there is at least 1 process so we dont get stuck here.
if(totalProcesses > 0){

	//Before we recieve the next message we want to check our queue and see if it is time for the process at the head of the q to recieve its message. 
	
	//If the clock has surpassed the time that the process at the head of the queue was placed into the queue + disk read/write time
	if((procArray[front].timeBlocked + 14000000) <= clock[1] && procArray[front].timeBlocked != -1){
		
		//We have already placed this requested page in the frame table so just need to send a message to this process, remove it from queue, and continue the loop.
		struct blockedProc returnedProc = dequeue();		
		
		buf.mtype = returnedProc.process + 1;
		
		if(fileLines < 10000 && previousProc != buf.procIndex){
			fprintf(filePtr, "Process %d removed from suspended queue, granted page %d TIME: %d sec %d ns\n", returnedProc.process, returnedProc.request, clock[0], clock[1]);
			fileLines++;
		}		
			
		if(msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1)
                	perror("in oss msgsend for blocked procs");		

	
		continue;
	}
	
	int qsize = size();
	
	if(currentProcs != qsize){
		
	if(msgrcv(msqid, &buf, sizeof(struct msgbuf), 20, 0) == -1){
		perror("msgrcv in oss");
		myhandler(1);
	}
	}
		
	//Right when we recieve the message we want to check and see if the process terminated
	if(buf.request == 99){
		
		if(fileLines < 10000){
		fprintf(filePtr, "Process %d has terminated. TIME: %d sec %d ns\n", buf.procIndex, clock[0], clock[1]);
		fileLines++;
		}

		childPids[buf.procIndex] = -2;

		currentProcs--;

		procs[buf.procIndex] = 0;
		//Loop through the processes page table and free blocks in frame table.
		int x;
		for(x = 0; x < 32; x++){
			//If the page has a frame index reset pageTable index back to defaults and remove it from frame table too.
			if(pageTable[buf.procIndex][x] != -1){
				frameTable[pageTable[buf.procIndex][x]].occupied = 0;	
				frameTable[pageTable[buf.procIndex][x]].dirty = -1;
				frameTable[pageTable[buf.procIndex][x]].process = -1;
				frameTable[pageTable[buf.procIndex][x]].page = -1;

				pageTable[buf.procIndex][x] = -1;
			}				


		}
	
		//Now that we reset that processes page table and the frames that it had taken up we want to reset the loop so we can recieve a different message.
		continue;
	}

	

	if(fileLines < 10000 && previousProc != buf.procIndex){
        fprintf(filePtr,"Process %d has requested operation %d to memory location %d TIME: %d sec %d ns\n", buf.procIndex, buf.operation, buf.request, clock[0], clock[1]);
        fileLines++;
        }
	
	//Divide request by 1000 to get page number in the page table.
	int pageNum = (buf.request / 1000);
	
	//If the page isnt in the frame table, we have a page fault
	//Need to place process in queue, then place that page in frame table.
	if(pageTable[buf.procIndex][pageNum] == -1){
		if(fileLines < 10000 && previousProc != buf.procIndex){
		fprintf(filePtr, "Page fault on process %d page %d. TIME: %d sec %d ns\n", buf.procIndex, pageNum, clock[0], clock[1]);
		fileLines++;
		}
		
		//Loop through frame table finding next available spot.
		int x;
		for(x = 0; x < 256; x++){
			
			//If frame in table isn't occupied then we set it and place the index in the page table also.
			if(frameTable[x].occupied == 0){
				frameTable[x].occupied = 1;
				//Set dirty bit if write
				if(buf.operation == 0){
					frameTable[x].dirty = 1;
			
				}						
				frameTable[x].page = pageNum;
				frameTable[x].process = buf.procIndex;
				pageTable[buf.procIndex][pageNum] = x;
				frameFlag = 1;
									
				break;		
			}	
		}

		//------ FIFO Algorithm--------

		//If the flag wasn't set to 1 then we know there isn't any free space in frame table.
		if(frameFlag == 0){
			//Replace the frame in the frametable with what was passed in that couldnt find a free spot above.
			frameTable[replaceFrame].occupied = 1;
			if(buf.operation == 0){
                        	frameTable[replaceFrame].dirty = 1;

                        }
                        frameTable[replaceFrame].page = pageNum;
                        frameTable[replaceFrame].process = buf.procIndex;
                        pageTable[buf.procIndex][pageNum] = replaceFrame;

			replaceFrame++;
			
			//If we have reached the end of the frame table then we start over.
			if(replaceFrame == 256){
				replaceFrame = 0;
			}
			
			//Increment the clock by and extra 10ms when we have to run the FIFO replacement algorithm on a page fault.
			clock[1]+= 10000000;

		}

		frameFlag = 0;
		
		//Now put the process in the queue and continue the larger while loop.
		//Build blocked process to be put into the queue
		struct blockedProc tempProc;
				
		tempProc.process = buf.procIndex;
		tempProc.timeBlocked = clock[1];
		tempProc.request = pageNum;
		tempProc.operation = buf.operation;

		if(buf.operation == 0){
                        clock[1] += 20000000;
                }
                else
                        clock[1] += 14000000;

 		
		//Queue the process that page faulted then continue the main loop starting it over. If we dont continue we will go through the process belowgranting the current process request that is in message buf.
		enqueue(tempProc);
				
		continue;
			
	}
	else{ //Will occur if the page is already in frame table just grant and add 10ns
		if(buf.operation == 0){//If write operation add more.
			clock[1] += 100;
		}
		else
			clock[1] += 10;

		if(fileLines < 10000 && previousProc != buf.procIndex){
        	fprintf(filePtr,"Granting process %d operation %d on memory location %d. TIME: %d sec %d ns\n", buf.procIndex, buf.operation, buf.request, clock[0], clock[1]);
       		fileLines++;
        	}
	
				

		buf.mtype = buf.procIndex + 1;
		buf.request = 0; //Since requests are from 1-32k we set to 0 when request approved.
	
		previousProc = buf.procIndex;
		
		if(msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1)
                perror("in oss msgsend");

	}
}

//Adding overhead for oss process.
clock[1] += 500000;

}//End while


if(fileLines < 10000){
	fprintf(filePtr,"Program will now terminate\n");
        fileLines++;
}
myhandler(1);

}

static void myhandler(int s){
	
	int msqid;
	key_t key;

	int i;
	for(i = 0; i < 18; i++){
		if(childPids[i] != -2){
			kill(childPids[i], SIGKILL);
		}
	}



	struct msgbuf buf;
	strcpy(buf.mtext, "hello\0");

	if ((key = ftok("msgq.txt", 'b')) == -1){
                perror("ftok");
                exit(1);
        }

	if((msqid = msgget(key, PERMS | IPC_CREAT)) == -1){
                perror("msgget");
                exit(1);
        }

	if(msgctl(msqid, IPC_RMID, NULL) == -1) {
                perror("msgctl");
                exit(1);
        }

	int shmid = shmget(CLOCK_SHMKEY, CLOCK_BUF, 0777);
	
	shmctl(shmid, IPC_RMID, NULL);
	
	system("rm msgq.txt");

	printf("Terminating from handler\n");

	exit(1);
}



//Functions for queue

bool isEmpty(){
return itemCount == 0;
}

bool isFull(){
return itemCount == 18;
}

int size() {
return itemCount;
}

void enqueue(struct blockedProc proc){
	
	if(!isFull()){
		if(rear == 17){
			rear = -1;
		}
	
		
		procArray[++rear] = proc;
		itemCount++;
	}

}

struct blockedProc dequeue(){

struct blockedProc proc = procArray[front];

	procArray[front].process = -1;
	procArray[front].timeBlocked = -1;
	procArray[front].request = -1;
	procArray[front].operation = -1;
	
	front++;

	if(front == 18){
		front = 0;
	}
	
	itemCount--;
	return proc;
}






















