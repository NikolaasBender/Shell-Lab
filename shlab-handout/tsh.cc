// 
// tsh - A tiny shell program with job control
// 
// Nikolaas Bender
// 106977096
// & Darius Dinnebeck
//


using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//=================================================================================================
//THIS IS ALL SETUP
//=================================================================================================
static char prompt[] = "tsh> ";
int verbose = 0;


//DEFINE JOB STATES
#define UNDEF 0 
#define FG 1    
#define BG 2    
#define ST 3    

//DEFINING PID GLOBALLY
int pid;

// YOU NEED TO IMPLEMENT THE FUNCTIONS EVAL, BUILTIN_CMD, DO_BGFG,
// WAITFG, SIGCHLD_HANDLER, SIGSTP_HANDLER, SIGINT_HANDLER
//
// THE CODE BELOW PROVIDES THE "PROTOTYPES" FOR THOSE FUNCTIONS
// SO THAT EARLIER CODE CAN REFER TO THEM. YOU NEED TO FILL IN THE
// FUNCTION BODIES BELOW.

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//==================================================================================================================
// MAIN - THE SHELL'S MAIN ROUTINE 
//==================================================================================================================
int main(int argc, char **argv) 
{
  // EMIT PROMPT (DEFAULT)
  int emit_prompt = 1; 

  // REDIRECT STDERR TO STDOUT (SO THAT DRIVER WILL GET ALL OUTPUT
  // ON THE PIPE CONNECTED TO STDOUT)
  dup2(1, 2);

  // PARSE THE COMMAND LINE
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    // PRINT HELP MESSAGE
    case 'h':             
      usage();
      break;
    // EMIT ADDITIONAL DIAGNOSTIC INFO
    case 'v':             
      verbose = 1;
      break;
    // DON'T PRINT A PROMPT (HANDY FOR AUTOMATIC TESTING)
    case 'p':             
      emit_prompt = 0;
      break;
    default:
      usage();
    }
  }


  //=============================================================
  // INSTALL THE SIGNAL HANDLERS
  //=============================================================

  // THESE ARE THE ONES YOU WILL NEED TO IMPLEMENT
  // CTRL-C
  Signal(SIGINT,  sigint_handler);
  // CTRL-Z
  Signal(SIGTSTP, sigtstp_handler);  
  // TERMINATED OR STOPPED CHILD
  Signal(SIGCHLD, sigchld_handler);  

  // THIS ONE PROVIDES A CLEAN WAY TO KILL THE SHELL
  Signal(SIGQUIT, sigquit_handler); 

  // INITIALIZE THE JOB LIST
  initjobs(jobs);

  // EXECUTE THE SHELL'S EVAL LOOP
  for(;;) {
    // READ COMMAND LINE
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    // DID USER TYPE CTRL-D? (EOF)
    if (feof(stdin)) {
      fflush(stdout);
      printf("EOF CLOSING\n");
      exit(0);
    }

    // EVALUATE COMMAND LINE
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  //CONTROL NEVER REACHES HERE
  exit(0); 
}










  
//=================================================================================================
// EVAL - EVALUATE THE COMMAND LINE THAT THE USER HAS JUST TYPED IN
// 
// IF THE USER HAS REQUESTED A BUILT-IN COMMAND (QUIT, JOBS, BG OR FG)
// THEN EXECUTE IT IMMEDIATELY. OTHERWISE, FORK A CHILD PROCESS AND
// RUN THE JOB IN THE CONTEXT OF THE CHILD. IF THE JOB IS RUNNING IN
// THE FOREGROUND, WAIT FOR IT TO TERMINATE AND THEN RETURN.  NOTE:
// EACH CHILD PROCESS MUST HAVE A UNIQUE PROCESS GROUP ID SO THAT OUR
// BACKGROUND CHILDREN DON'T RECEIVE SIGINT (SIGTSTP) FROM THE KERNEL
// WHEN WE TYPE CTRL-C (CTRL-Z) AT THE KEYBOARD.
//=================================================================================================
void eval(char *cmdline) 
{
  // PARSE COMMAND LINE
  // THE 'ARGV' VECTOR IS FILLED IN BY THE PARSELINE
  // ROUTINE BELOW. IT PROVIDES THE ARGUMENTS NEEDED
  // FOR THE EXECVE() ROUTINE, WHICH YOU'LL NEED TO
  // USE BELOW TO LAUNCH A PROCESS.
  char *argv[MAXARGS];
  pid_t pid;
  
  // THE 'bg' VARIABLE IS TRUE IF THE JOB SHOULD RUN
  // IN BACKGROUND MODE OR FALSE IF IT SHOULD RUN IN FG
  int bg = parseline(cmdline, argv); 

  //NOTHING WAS PASSED IN
  if (argv[0] == NULL)  
    return;   

  //PASS argv TO builtin_cmd 
  //IF ITS NOT WE JUST GO IN AND DO STUFF

  //CHILD BLOCKING SETUP
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTSTP);

  //IF ITS NOT A BUILT IN CMD WE HAVE TO EXECUTE IT
  if(!builtin_cmd(argv)){
   
    //BLOCK SIGS
    sigprocmask(SIG_UNBLOCK, &mask, NULL);   

    //IF THE TASK SHOULD BE BACKGROUND
    if(bg){
      if((pid = fork()) == 0){
        //UNBLOCK SIGS AND SET PID FOR CHILD
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        setpgid(0,0);

        //GHETTO ASS TRY CATCH WHILE EXECUTING
        if(execv(argv[0],argv) < 0){
          printf("%s: Command not found. \n", argv[0]);
          exit(0);
        }
        return;
      }
      //ADD JOB TO STRUCT
      addjob(jobs, pid, BG, cmdline);
      //PRINT JOB INFO
      struct job_t* job = getjobpid(jobs, pid);
      printf("[%d] (%d) %s", job -> jid, job -> pid, job -> cmdline);

    }else{
      if((pid = fork()) == 0){
        //BLOCK CHILD SIGS
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        setpgid(0,0);

        //GHETTO TRY CATCH AGAIN
        if(execv(argv[0],argv) < 0){
          printf("%s: Command not found. \n", argv[0]);
          exit(0);
        }
        return;
      }
      //ADD JOB TO STRUCT
      addjob(jobs,pid,FG,cmdline);
      //UNBLOCK AND WAIT FOR PROCESS
      sigprocmask(SIG_UNBLOCK, &mask, NULL);
      waitfg(pid);
    }
  }
  return;
}










//=================================================================================================
// builtin_cmd - DEALS WITH quit, fg/bg, jobs
//=================================================================================================
int builtin_cmd(char **argv) 
{
  string cmd(argv[0]);


  //DEALS WITH QUIT
  if(cmd == "quit"){
    //printf("See ya later\n");
    exit(0);
  }


  //DEALS WITH JOBS LIST
  if(cmd == "jobs"){
    listjobs(jobs);
    return 1;
  }


  //DEALS WITH FG BG
  if(cmd == "fg" || cmd == "bg"){
    do_bgfg(argv);
    return 1;
  }


  //NOT A BUILTIN COMAND
  return 0;   
}



//=================================================================================================
// DO_BGFG - EXECUTE THE BUILTIN BG AND FG COMMANDS
//=================================================================================================
void do_bgfg(char **argv) 
{
  struct job_t *jobp=NULL;
    
  //IGNORE COMMAND IF NO ARGUMENT
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  // PARSE THE REQUIRED 'PID' OR '%JID' ARG
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }     
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //=====================================================================
  // YOU NEED TO COMPLETE REST. AT THIS POINT,
  // THE VARIABLE 'jobp' IS THE JOB POINTER
  // FOR THE JOB 'ID' SPECIFIED AS AN ARGUMENT.
  //
  // YOUR ACTIONS WILL DEPEND ON THE SPECIFIED COMMAND
  // SO WE'VE CONVERTED argv[0] TO A STRING (cmd) FOR
  // YOUR BENEFIT.
  //====================================================================
  string cmd(argv[0]);

  int childSig;

  //CHILD BLOCKING SETUP
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTSTP);

  //FOREGROUND
  if(cmd == "fg"){
    //CHECK IF THE JOB IS STOPPED
    if(jobp -> state == ST){
      kill(-jobp -> pid, SIGCONT);
    }
    //SETTING THE JOB TO BE FOREGROUND
    jobp -> state = FG;
    //WAIT FOR IT TO FINISH
    waitfg(jobp -> pid);
  //BACKGROUND
  }
  if(cmd == "bg"){
    //CONTINUE JOB
    kill(-jobp -> pid, SIGCONT);
    //CHANGE JOB STATE TO BE bg
    jobp -> state = BG;
    printf("[%d] (%d) %s",jobp -> jid, jobp -> pid, jobp->cmdline);
  }

  return;
}



//=================================================================================================
// WAITFG - BLOCK UNTIL PROCESS PID IS NO LONGER THE FOREGROUND PROCESS
//=================================================================================================
void waitfg(pid_t pid){
  for(;;) {
    //SETUP A STRUCT TO KEEP TRACK OF JOBS
    struct job_t *job = getjobpid(jobs,pid);
    if ( job == NULL ) {
      return;
    }
    if (job -> state != FG) {
      return;
    }
    sleep(100);
  
  }
  return;
}



//=======================================================================================================
//=======================================================================================================
// SIGNAL HANDLERS
//=======================================================================================================
//=======================================================================================================



//=================================================================================================
// SIGCHLD_HANDLER - THE KERNEL SENDS A SIGCHLD TO THE SHELL WHENEVER
//     A CHILD JOB TERMINATES (BECOMES A ZOMBIE), OR STOPS BECAUSE IT
//     RECEIVED A 'SIGSTOP' OR 'SIGTSTP' SIGNAL. THE HANDLER REAPS ALL
//     AVAILABLE ZOMBIE CHILDREN, BUT DOESN'T WAIT FOR ANY OTHER
//     CURRENTLY RUNNING CHILDREN TO TERMINATE.  
//=================================================================================================
void sigchld_handler(int sig) 
{
  pid_t pid;
  int status = -1;
  
  while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0){
    
    //DELETE JOB SIGNAL FOR SIGCHLD
    if ( WIFEXITED(status) ) { 
      //ERASE THE CHILD SIGNAL FROM JOBS LIST LIKE ME OUT OF THE FAMILY CHRISTMAS PHOTO
      deletejob(jobs, pid); 
      
      //THE DELETE SIGNAL FOR SIGINT
    }else if( WIFSIGNALED(status) ){ 
      printf("Job [%d] (%d) terminated by signal %d \n",pid2jid(pid),pid,WTERMSIG(status));
      //DELETE CHILD FROM JOBLIST
      deletejob(jobs, pid); 
      
      //STOP SIGNAL FOR SIGTSTP
    }else if( WIFSTOPPED(status) ){
      printf("Job [%d] (%d) stopped by signal %d \n",pid2jid(pid),pid,WSTOPSIG(status));
      job_t* temp = getjobpid(jobs, pid);
      temp -> state = ST;
    }
  }
  return;
}



//=================================================================================================
// SIGINT_HANDLER - THE KERNEL SENDS A 'SIGINT' TO THE SHELL WHENVER THE
//    USER TYPES CTRL-C AT THE KEYBOARD.  CATCH IT AND SEND IT ALONG
//    TO THE FOREGROUND JOB.  
//=================================================================================================
void sigint_handler(int sig) 
{
  //SAVE FOREGROUND TASK
  pid_t temp = fgpid(jobs);
  //IF THERE IS A FG TASK... KILL IT
  if(temp != 0){
    kill(-temp, SIGTSTP);
  }
  return;
}



//=================================================================================================
// SIGTSTP_HANDLER - THE KERNEL SENDS A 'SIGTSTP' TO THE SHELL WHENEVER
//     THE USER TYPES CTRL-Z AT THE KEYBOARD. CATCH IT AND SUSPEND THE
//     FOREGROUND JOB BY SENDING IT A 'SIGTSTP'.  
//=================================================================================================
void sigtstp_handler(int sig) 
{
  //SAVE FOREGROUND TASK
  pid_t temp = fgpid(jobs);
  //IF THERE IS A FG TASK... KILL IT
  if(temp != 0){
    kill(-temp, SIGTSTP);
  }
  return;
}



//=======================================================================================================
//=======================================================================================================
// END SIGNAL HANDLERS
//=======================================================================================================
//=======================================================================================================




