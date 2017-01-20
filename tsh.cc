//
// tsh - A tiny shell program with job control
//
//Haoyu Li
//Code partially adopted from Nicky

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

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
//

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

// Helper prototypes
int pid2jid(pid_t pid);

//
// main - The shell's main routine
//
int main(int argc, char **argv)
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler);

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  }

  exit(0); //control never reaches here
}

/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
//
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline)
{
  /** This fxn is to analysis and implement the users input **/
  /** Is the user types 'quit', 'bg', 'fg', 'jobs' ,it will implement immediately **/
  /** , or it will fork a child process and do the task.**/
  /** If the task is the fg task, then it needs to wait until it's done and return **/

  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];
  pid_t pid;

  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv);
  if (argv[0] == NULL)
    return;                      /* ignore empty lines */

	sigset_t mask;               /* mask for signal */
	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);   /* Add a signal to the set,
                                    The signal mask of a signal handler means that while the signal handler is executing,
                                    the signals that are in the mask will be blocked  */
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTSTP);

	if(!builtin_cmd(argv))       /* not a build in cmd */
	{

		sigprocmask(SIG_BLOCK, &mask, NULL);
		//Before fork, it has to block SIGCHLD. After finish work,
        //call sigprocmask on SIG_UNBLOCK to cancel the block.
        //Because when we fork, the child process inherited former status
        //also, in case to avoid the 'Race' mistake; addjob and deletejob random implement

		if(bg)
		{
			if((pid = fork()) == 0) // Child
            {
				sigprocmask(SIG_UNBLOCK, &mask, NULL);
				/** unblock the SIGCHLD in child **/

				setpgid(0,0);           //puts the child in a new process group
				/** After fork, the child will have an unique PID, and implement in serpgid(0,0) **/
				/** ,so when we do ctrl + c or ctrl + z, it won't affect bg **/
				/** If we don't do this, the bg and fg will receive child process **/
                //printf("Hello");

                if(execv(argv[0],argv) < 0)     // if this cant find the cmd then it cannot be found at all

                //The execv() call cannot specify the env argument
                //to the new process image with the env argument
                {
                    printf("%s: Command not found. \n", argv[0]);
                    exit(0);
				}

				return;
			}

			addjob(jobs, pid, BG, cmdline);         /* add job into jobs */

			struct job_t* job = getjobpid(jobs,pid);
			printf("[%d] (%d) %s",job -> jid,job -> pid, job -> cmdline);


		}
		else // Foreground
		{

			if((pid = fork()) == 0) // Child
            {
				sigprocmask(SIG_UNBLOCK, &mask, NULL);
				setpgid(0,0);

				if(execv(argv[0],argv) < 0)     // if this cant find the cmd then it cannot be found at all
                {
					printf("%s: Command not found. \n", argv[0]);
					exit(0);
				}
				return;

			} // Parent

			addjob(jobs, pid, FG, cmdline);
			sigprocmask(SIG_UNBLOCK, &mask, NULL);
			waitfg(pid);
		}
	}

  return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv)
{
  string cmd(argv[0]); // cmd contains the first argument

  if(cmd.compare("quit") == 0)
  {
      exit(0);
  }
  else if((cmd.compare("fg") == 0) || (cmd.compare("bg") == 0 ))
  //If a foreground or background
  {
	do_bgfg(argv);
	return 1;
  }

  else if(cmd.compare("jobs") == 0)
  // Print out the job list
  {
	listjobs(jobs);
	return 1;
  }

  return 0;
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv)   // this is for trace09 and trace 10
{
  /** Entering %num means jobID, num means PID **/
  struct job_t *jobp = NULL;

  /* Ignore command if no argument */
  if (argv[1] == NULL)
  {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }

  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0]))
  {
    pid_t pid = atoi(argv[1]);

    if (!(jobp = getjobpid(jobs, pid)))
    {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%')  /* the argument is a job id */
  {
    int jid = atoi(&argv[1][1]);

    if (!(jobp = getjobjid(jobs, jid)))
    {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }
  else
  {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
  string cmd(argv[0]);

	if(cmd.compare("fg") == 0)
    {
		if(jobp -> state == ST)
        {
			kill(-jobp -> pid, SIGCONT);
		}

		jobp -> state = FG;
		waitfg(jobp -> pid);        //wait to finish

	}
	else if(cmd.compare("bg") == 0 )        /* set job state, do it in bg or fg */
    {
		kill(-jobp -> pid, SIGCONT);        /* send the SIGCONT to the pid */
		jobp -> state = BG;
		printf("[%d] (%d) %s",jobp -> jid, jobp -> pid, jobp -> cmdline);
	}

}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
  //One of the tricky parts of the assignment is deciding on the allocation of work between the waitfg and sigchld handler functions. We recommend the following approach:
  //In waitfg,use a busy loop around the sleep function.
  //In sigchldhandler,use exactly one callto waitpid.

  /** This is used to watit for fg child process done **/

  /** When the child process is done, it sends signal to SIGCHLD. After that, **/
  /** it will be dealt by sigchld_handler fxn and reap zombies and delete the fg process in jobs **/
  while(pid == fgpid(jobs))
  {
      sleep(0);
      /** doing sleep fxn to wait if the jobs list still has fg process **/
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.
//
void sigchld_handler(int sig)
{
  pid_t pid;
  int status = -1;

  while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0)
  /** If  status is not NULL, wait() and waitpid() store status infor-
   mation in the int to which  it  points. **/

  /** the WNOHANG and WUNTRACED arguments are to determine if there's a stopping or terminate process in the process**/
  /**If there is, return pid. If not, return immediately **/
  /** the &status checks why it stops or paused when it returns **/
  {
	    // the delete job signal for sigchld
		if (WIFEXITED(status)) /* check if child terminated normally */
        {
			deletejob(jobs, pid);
		}
        // The delete signal for sigint
		else if(WIFSIGNALED(status)) /* checks if child was terminated by a signal that was not caught*/
		{
			printf("Job [%d] (%d) terminated by signal %d \n",pid2jid(pid) ,pid ,WTERMSIG(status));
			deletejob(jobs, pid);

		}
        //Stop signal for sigtstp
		else if(WIFSTOPPED(status) ) /* check if child process that caused return is currently stopped */
        {
			printf("Job [%d] (%d) stopped by signal %d \n",pid2jid(pid) ,pid ,WSTOPSIG(status));
			job_t* temp = getjobpid(jobs, pid);
			temp -> state = ST;
		}
  }
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.
//
void sigint_handler(int sig)
{
    /* Set up a temporary structure so that we can save the FG task  */
	pid_t temp = fgpid(jobs);

    /* If there was a foreground task then Reap it                   */
	if(temp != 0)
	{
		kill(-temp, SIGINT);
		/**let the sigchld_handler to delete the job in jobs **/
	}

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.
//
void sigtstp_handler(int sig)
{
    /* Set up a temp so that we can save the FG Task  */
	pid_t temp = fgpid(jobs);

    /* If there was a foreground task then pause it                  */
	if(temp != 0)
	{
	    kill(-temp, SIGTSTP);
	}

  return;
}

/*********************
 * End signal handlers
 *********************/
