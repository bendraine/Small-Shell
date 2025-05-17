//Name: Benjamin Draine

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>	
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#define MAX_CMD_LEN 2049 
#define MAX_ARGS 21			
#define MAX_BG_PROCESSES 1000

pid_t bg_pids[MAX_BG_PROCESSES]; //keep track of children in bg
int bg_pid_count = 0; 

char pid_string[20];  //holds pid for $$ swap

int last_exit_status = 0;
int last_term_sig = 0;
int term_sig_bool = 0;

volatile sig_atomic_t foreground_only_mode = 0;

//Ctrl-Z handler
void SIGTSTP_handler(int signo){
	//we use write() because its reentrant
	if(foreground_only_mode){
		write(1,"\nExiting foreground only mode\n", 30);
		foreground_only_mode = 0;
	}
	else{
		write(1,"\nEntering foreground only mode\n", 31);
		foreground_only_mode = 1;
	}
}

//check for white space in input line, return 1 if entire string is whitespace, 0 if not
int has_white_space(char* line){
	for(int i = 0; i < strlen(line); i++){
		if(!isspace(line[i])){
			return 0;
		}
	}
	return 1;
}

int main() {

	//turn our PID into a string so we can swap with $$
	sprintf(pid_string, "%d", getpid());

	//sigint_sa  is handler for SIGINT and sa_tstp is handler for SIGTSTP
	struct sigaction sigint_sa = {0}, sa_tstp = {0};

	sigint_sa.sa_handler = SIG_IGN;  //ignore the signal
	sigemptyset(&sigint_sa.sa_mask); 
	sigint_sa.sa_flags = 0; 

	//specify ignore SIGINT or Ctrl-C as instructed 
	if(sigaction(SIGINT, &sigint_sa, NULL) == -1){
		printf("SIGINT handler failed");
		return 1; 
	}

	sa_tstp.sa_handler = SIGTSTP_handler; //call signal handler when signal is recieved 
	sigemptyset(&sa_tstp.sa_mask);
	sa_tstp.sa_flags = 0;

	//specify to call handler when Ctrl-Z or SIGTSTP is recieved as instructed
	if(sigaction(SIGTSTP, &sa_tstp, NULL) == -1){
		printf("SIGTSTP handler failed");
		return 1;
	}

	char line[MAX_CMD_LEN]; //character array to story command line input
	memset(line, 0, MAX_CMD_LEN); //initialize array the zeros

	while(1){
		pid_t child_pid;  //pid for child
		int b_status; //track how process was killed

		//loop through all child processes that have terminated and return immediately if no child exits
		//waitpid returns child pid
		while ((child_pid = waitpid(-1, &b_status, WNOHANG)) > 0) {
			//loop through background processes PIDs to find which one ended
			for (int i = 0; i < bg_pid_count; i++) {
				//found the terminated background process
            	if (bg_pids[i] == child_pid) {
					if (WIFEXITED(b_status)) {  //child exited normally 
						printf("Background process %d terminated with exit status %d\n", child_pid, WEXITSTATUS(b_status));
					} else if (WIFSIGNALED(b_status)) { //child was terminated by signal
						printf("Background process %d terminated by signal %d\n", child_pid, WTERMSIG(b_status));
					}
					//remove terminated PID from the array
					bg_pids[i] = bg_pids[bg_pid_count - 1];  //replace index of removed PID with last in the array
					bg_pid_count--; //reduce count of background processes by 1
					break; //no need to keep going through loop once found
				}
			}
        }

		printf(":");  //command prompt
		fflush(stdout);  //ensure prompt appears immediately

		//read line of input from user into 'line'
		if (fgets(line, MAX_CMD_LEN, stdin) == NULL) {
			//if fgets returns NULL or end of file
			if (feof(stdin)) { //if end of file is reached then then exit
                printf("\nExit requested via EOF\n"); 
                break;
			//if fgets was interrupted by signal then reprompt
            } else if (errno == EINTR) { 
                continue;
			//print error message for any other error and prompt again
            } else {
                perror("fgets");
                continue;
            }
        }

		//get length of input line
		size_t len = strlen(line);

		//if line ends in newline character remove it and replace with null terminator
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
		}
        
		//if line is a comment then ignore and prompt again
		if (line[0] == '#' || line[0] == '\0') {
			continue;
		}

		//make sure to continue to next prompt if only white space is given
		if(has_white_space(line) == 1){
			continue;
		}

		//create buffer to hold command line after expansion of $$
		char expanded_line[MAX_CMD_LEN] = {0};

		//pointer for original input
		char* ptr = line;

		//loop through each character until null terminator is reached
		while (*ptr) {

			//if current and next characters are $$ then replace with shells PID
			if (*ptr == '$' && *(ptr + 1) == '$') {
				strcat(expanded_line, pid_string); //append the pid
				ptr += 2; //move past the $$ characters
			} else {
				strncat(expanded_line, ptr, 1); //otherwise copy current character 
				ptr++; //go to next character
			}
		}

		char* args[MAX_ARGS] = {0};  //array of pointers to hold command args set to NULL
		int arg_count = 0;  //counter for number of args
		char* token = strtok(expanded_line, " ");  //tokenize expanded command line with spaces
		
		//input and output redirection file names
		char* input_file = NULL;
		char* output_file = NULL;
		
		//loop through tokens until they are null or we reach max args
		while (token != NULL && arg_count < MAX_ARGS - 1) {
			
			//check for input redirection
			if (strcmp(token, "<") == 0) {
				//next token should be input file name
			  	token = strtok(NULL, " ");
				input_file = token; //store input file
			} else if (strcmp(token, ">") == 0) { //check for output redirection
				//next token should be output file name
				token = strtok(NULL, " ");
				output_file = token;  //store output file
			} else {
				args[arg_count++] = token;  //add regular arg to array
			}
			token = strtok(NULL, " "); //get next token
		}

		//add null terminator to args array so exec can know when args ends
		args[arg_count] = NULL;

		int is_background = 0; //var to track if command should be run in background

		//check if last arg is & to see if we should run in the background
		if (arg_count > 0 && strcmp(args[arg_count - 1], "&") == 0) {

			//only allow background mode if foreground only mode is not active
			if(!foreground_only_mode){
				is_background = 1;  //mark background process to be running
			}
			args[arg_count - 1] = NULL;  // Remove the '&' from the arguments
			arg_count--; //decrement arg count
		}

		//if command is 'exit' kill background processes before exiting the shell
		if (strcmp(args[0], "exit") == 0) {
			for(int i = 0; i<bg_pid_count; i++) {
				kill(bg_pids[i], SIGKILL);  //sigkill all background processes
			}
			break; // Exit the shell
		} else if (strcmp(args[0], "cd") == 0) {  //if command is cd
			//if an arg is provided change to that directory
			if (args[1] != NULL) { 
			if (chdir(args[1]) != 0) {
				perror("cd failed"); //print error if change fails
			}
			} else {
        		chdir(getenv("HOME"));  // Default to HOME if no arg
    		}
		} else if (strcmp(args[0], "status") == 0) {  //if command is status
			//print 0 if last exit status was zero
			if (term_sig_bool == 0 && last_exit_status == 0) {
				printf("Exit value : 0\n"); 
			} else if (term_sig_bool == 0) {  //print last exit status if non zero
				printf("Exit value: %d\n", last_exit_status);
			} else if (term_sig_bool == 1) {  //print signal that terminated the last foreground process
				printf("Terminated by signal: %d\n", last_term_sig);
			}
		} else { 
			// For other commands, fork a child process to execute the non built in commands while the shell runs independently
			pid_t pid = fork();
			if (pid == -1) {
				perror("fork failed"); //print error for fork failure
				continue; //continue to next shell loop iteration
			} else if (pid == 0) {  //enter into child process

				//children must ignore Ctrl-Z or SIGTSTP
				struct sigaction sigtstp_ignore = {0};
                sigtstp_ignore.sa_handler = SIG_IGN;  //set handler to ignore
                sigemptyset(&sigtstp_ignore.sa_mask);
                sigtstp_ignore.sa_flags = 0;
                sigaction(SIGTSTP, &sigtstp_ignore, NULL); //struct sigaction set to ignore SIGTSTP as instructed

				//if in foreground only mode
				if(foreground_only_mode || !is_background){
					//change foreground child process behavior for SIGINT to default
					struct sigaction sigint_default = {0};
					sigint_default.sa_handler = SIG_DFL; //set handler to default
					sigemptyset(&sigint_default.sa_mask);
					sigint_default.sa_flags = 0;
					sigaction(SIGINT, &sigint_default, NULL); //struct sigaction set to default SIGINT behavior (terminate)
				} else {
					//change background chld process behavior for SIGINT to ignore
					struct sigaction sigint_ignore = {0};
					sigint_ignore.sa_handler = SIG_IGN; //set handler to ignore
					sigemptyset(&sigint_ignore.sa_mask);
					sigint_ignore.sa_flags = 0;
					sigaction(SIGINT, &sigint_ignore, NULL);  //struct sigaction set to ignore SIGINT as instructed
				}

				//if user gave input redirection file after <
				if (input_file != NULL) {
					//open the input file for reading only (O_RDONLY) and store the file descriptor (-1 if fails)
					int in_fd = open(input_file, O_RDONLY); 
					//open returned -1 because it failed to open or doesnt exist
					if (in_fd == -1) { 
						perror("input redirection failed"); //print error describing the failure
						exit(1);  //exit child process since we cant go on with input redirection
					}

					//duplicate the file descriptor 'in_fd' into the standard input (file descriptor 0)
					//reads from stdin will now come from the input file
					dup2(in_fd, 0);
					//in_fd and 0 refer to same file description that is open so we close the old one to avoid having 2 open
					close(in_fd);
				}

				//if user gave output redirection file after >
				if (output_file != NULL) {
					//open the output file for writing only (O_WRONLY) and store the file descriptor (-1 if it fails)
					//if file doesn't exist then create it (O_CREAT)
					//if file does exist then truncate it (O_TRUNC)
					//set permissions to rw-r--r--
					int out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					//open returned -1 because it failed to open/create or permissions denied
					if (out_fd == -1) {
						perror("output redirection failed"); //print error describing the failure
						exit(1); //exit the child process since we cant go on with output redirection
					}
					
					//duplicate the file descriptor 'out_fd' into the standard output (file descriptor 1)
					//writes to stdout will now go to the output file
					dup2(out_fd, 1);
					//out_fd and 1 refer to same file description that is open so we close the old one to avoid having 2 open
					close(out_fd);
				}
				
				//if command is running in background
				if (is_background) {
					//if input file was not given then redirect standard input to /dev/null as instructed
					if (input_file == NULL) {
						int in_fd = open("/dev/null", O_RDONLY); //open /dev/null for empty input as instructed
						//duplicate /dev/null onto standard input (file descriptor 0)
						//reads will now come from /dev/null
						dup2(in_fd, 0); 
						close(in_fd); //close duplicate file descriptor
					}
					//if output file was not given then redirect standard output to /dev/null as instructed
					if (output_file == NULL) {
						int out_fd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644); //open /dev/null for empty output as instructed
						//duplicate /dev/null onto standard output (file descriptor 1)
						//writes will now go to /dev/null
						dup2(out_fd, 1); 
						close(out_fd); //close duplicate file descriptor
					}
				}

				// Execute the command, args[0] is the command and args is the full argument list
				execvp(args[0], args);
				perror("execvp failed");  //print error if execvp() fails
				exit(1); //exit with status one for failure
			} else {
				int status; //status code tracker

				//if process is in foreground
				if (foreground_only_mode || !is_background) {
					// Wait for child process (pid) to complete to avoid zombie processes and store the status code
					waitpid(pid, &status, 0);  
					
					//if process ended normally, save the last exit status
					if (WIFEXITED(status)) {
						last_exit_status = WEXITSTATUS(status); //save last exit status
						term_sig_bool = 0;  //no termination by signal
					} else if (WIFSIGNALED(status)) { //if process was terminated by signal
						last_term_sig = WTERMSIG(status); //save signal that terminated it
						term_sig_bool = 1; //yes termination by signal
						printf("Foreground process %d terminated by signal %d\n", pid, last_term_sig);
					}
                } else { //if in background 

					//if there is room for more background processes, save the pid
					if (bg_pid_count < MAX_BG_PROCESSES) {
						bg_pids[bg_pid_count++] = pid;  //add pid to array
					}
                    printf("Background process started with PID %d\n", pid);
				}
			}
		}
		fflush(stdout); //ensure output is flushed to terminal
	}
	return 0;
}


