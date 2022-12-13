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

//Shared memory for clock
#define CLOCK_SHMKEY 511598

#define PERMS 0644

//Buffer for clock
#define CLOCK_BUF (sizeof(int) * 2)

struct msgbuf{
long mtype;
char mtext[15];
int request;
int operation; //1 for read 0 for write
int procIndex;
};

static void myhandler(int s);

int main (int argc, char** argv){

//Vars for message q
int msqid;
int len;
struct msgbuf buf;
buf.mtype = 1;
key_t key;
int index = atoi(argv[1]);

//Set up message q
if((key = ftok("msgq.txt", 'b')) == -1){
		perror("ftok");
		exit(1);
}

if((msqid = msgget(key, PERMS)) == -1){
		perror("msgget");
		exit(1);
}


signal(SIGINT, myhandler);


//Attach to shared mem for clock
int shmid = shmget(CLOCK_SHMKEY, CLOCK_BUF, 0777);

if(shmid == -1){
	perror("in user shmget for clock");
	exit(1);
}

char* paddr = (char*)(shmat(shmid, 0, 0));
int* clock = (int*)(paddr);

int x = 0;
int oneK = 1000;
int termFlag = 0;

srand(time(0));

printf("I am process: %d\n", index);

while(1){

buf.request = (rand() % 32000 + 1);

int operation = (rand() % 8 + 1);

//If operation is 1-6 treat it as a read, biasing it towards reads 75%. 
if(operation == 7 || operation == 8){
	buf.operation = 0; //Write
}
else{
	buf.operation = 1; //Read
}

buf.mtype = 20;

buf.procIndex = index;

//Roll to see if we should terminate after every 1k memory references. Giving this a 40% chance.
if(x >= oneK){
	
	
	int terminate = (rand() % 10 + 1);

	printf("\n\nproc %d terminate is %d\n\n", index , terminate);
		
	if(terminate == 1 || terminate == 2 || terminate == 3 || terminate == 4 || terminate == 5){
		//If we are terminating send msg to OSS and call handler.
		buf.request = 99;
		
		if(msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1){
 		       perror("msgsend");
		}
		
		myhandler(1);
		
	}
	else{
		termFlag = 1; //Means we reached 1k mem references but didnt terminate so set flag so that we can increment 1k to 2k now.
	}
	
}

if(termFlag == 1){
oneK += 1000;
termFlag = 0;
}

if(msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1){
	perror("msgsend");
}
/*
if(index > 0){
printf("Message sent by process %d request is %d\n", index, buf.request);
}
*/
if(msgrcv(msqid, &buf, sizeof(struct msgbuf), (index + 1), 0) == -1){
        perror("msgrcv in user");
        exit(1);
}


/*
if(index > 0){
printf("Message received by process %d request is %d\n", index, buf.request);
}
*/
x++;
}//End while




myhandler(1);

}


static void myhandler(int s){

int shmid = shmget(CLOCK_SHMKEY, CLOCK_BUF, 0777);
char* str = (char*) shmat(shmid, 0, 0);
shmdt(str);

exit(1);
}
