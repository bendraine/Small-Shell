This small shell is able to:
 1. Provide a prompt for running commands
 2. Handle blank lines and comments, which are lines beginning with a # character
 3. Provide expansion for the $$ shell variable
 4. Support three built-in commands: exit, cd, and status
 5. Execute other commands byforking new processes and calling an exec()-family function (e.g., execvp())
 6. Support input and output redirection
 7. Support running commands in foreground and background processes
 8. Implement custom handlers for two signals: SIGINT and SIGTSTP
