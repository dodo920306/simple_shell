#ifndef _POSIX_C_SOURCE
	#define  _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h> 
#include <sys/types.h>
#include <sys/stat.h>

#define PIPE_BUFSIZE 8
#define TOKEN_BUFSIZE 64
#define TOKEN_DELIM " \t\n"
#define LINE_BUFSIZE 1024
#define RECORD_BUFSIZE 16

int builtin_amount();
int help(char **);
int cd(char **);
int echo(char **);
int shexit(char **);
int record(char **);
int replay(char **);
int mypid(char **args);
int pipe_execute(char **);
char **pipe_split(char *);
int execute(char **);
int launch(char **);
char **split(char *);
char *read_line();
void loop();

struct queue{
    char *line;
    struct queue *next;
};
typedef struct queue Queue;

char *builtin_name[] = {
	"help",
	"cd",
	"echo",
	"record",
	"replay",
	"mypid",
	"exit"
};

int (*builtin_func[]) (char **) = {
	&help,
	&cd,
	&echo,
	&record,
	&replay,
	&mypid,
	&shexit
};

int pipe_amount;
char **cmd_record;
Queue *tail;

int builtin_amount() {
	return sizeof(builtin_name) / sizeof(char *); 
}

int help(char **args) {
	printf("--------------------------------------------------------\n");
	printf("my little shell\n");
	printf("Type program names and arguments, and hit enter.\n\n");
	printf("The following are built in:\n");
	printf("1: help:    show all build-in function info\n");
	printf("2: cd:      change directory\n");
	printf("3: echo:    echo the strings to standard output\n");
	printf("4: record:  show last-16 cmds you typed in\n");
	printf("5: replay:  re-execute the cmd showed in record\n");
	printf("6: mypid:   find and print process-ids\n");
	printf("7: exit:    exit shell\n");
	printf("--------------------------------------------------------\n");
	return 1;
}

int cd(char **args) {
	if (args[1] == NULL) {
		fprintf(stderr, "sh: expect arguments\n");
	}
	else {
		if (chdir(args[1])) {
			perror("sh");
		}
	}
	return 1;
}

int echo(char **args) {
    if (!args[1])   return 1;
    
	for (int i = (!strcmp(args[1], "-n")) ? 2 : 1; args[i]; i++) {
		printf("%s" ,args[i]);
		if (args[i + 1])   printf(" ");
	}
	if (strcmp(args[1], "-n"))	printf("\n");
	return 1;
}

int shexit(char **args) {
	printf("my little shell: See you last time.\n");
	return 0;
}

int record(char **args) {
    Queue *tmp = tail;
    char *lines[RECORD_BUFSIZE];
    int i;
    for (i = 0; tmp && i < RECORD_BUFSIZE; i++){
        lines[i] = tmp->line;
        tmp = tmp->next;
    }
    
    for (int j = 1; i && j <= RECORD_BUFSIZE; j++){
        if (j < 10) printf(" ");
        printf("%d: %s\n", j, lines[--i]);
    }
    return 1;
}

int mypid(char **args) {
    if (!args[1]){
        fprintf(stderr, "mypid: expect flag\n");
        return 1;
    }
    char flag = args[1][1];
    const int PPID_POS = 6;
    pid_t pid;
    
    switch (flag){
        case 'i':
            pid = getpid();
            printf("%d\n", pid);
            return 1;
        case 'p':
            if (!args[2]){
                fprintf(stderr, "mypid: expect pid\n");
            }
            else{
                char filename[30] = "/proc/";
                strcat(filename, args[2]);
                strcat(filename, "/status");
                FILE* ptr = fopen(filename, "r");
                if (!ptr){
                    fprintf(stderr, "mypid: wrong process\n");
                    return 1;
                }
                char buf[64];
                int i = 0;
                while (fgets(buf, sizeof(buf), ptr)){
                    if (i == PPID_POS){
                        printf("%s\n", split(buf)[1]);
                        break;
                    }
                    i++;
                }
            }
            return 1;
        case 'c':
            if (!args[2]){
                fprintf(stderr, "mypid: expect pid\n");
            }
            else{
                char filename[30] = "/proc/";
                strcat(filename, args[2]);
                strcat(filename, "/task/");
                strcat(filename, args[2]);
                strcat(filename, "/children");
                FILE* ptr = fopen(filename, "r");
                if (!ptr){
                    fprintf(stderr, "mypid: wrong process\n");
                    return 1;
                }
                char buf[6];
                while (fscanf(ptr, "%s", buf) == 1)    printf("%s\n", buf);
            }
            return 1;
        default:
            fprintf(stderr, "mypid: wrong flag\n");
            return 1;
    }
}

int replay(char **args) {
    if (!args[1])   fprintf(stderr, "sh: expect arguments\n");
    Queue *tmp = tail;
    char *lines[RECORD_BUFSIZE];
    int i, j;
    for (i = 0; tmp && i < RECORD_BUFSIZE; i++){
        lines[i] = tmp->line;
        tmp = tmp->next;
    }
    
    for (j = 1; i && j < atoi(args[1]); j++){
        --i;
    }
    
    if (j == atoi(args[1]) && i){
        --i;
        char *cmd = malloc(strlen(lines[i]) + 1);
        tail->line = realloc(tail->line, strlen(lines[i]) + 1);
        strcpy(cmd, lines[i]);
        strcpy(tail->line, cmd);
        return (pipe_execute(pipe_split(cmd)));
    }
    else{
        fprintf(stderr, "replay: wrong args”\n");
        return 1;
    }
}

int pipe_execute(char **pipes) {
    if (pipe_amount == 1)   return(execute(split(pipes[0])));
    
	int pipe_fd[pipe_amount][2];
	char ***pipe_args = malloc(pipe_amount * sizeof(char **));
	for (int i = 0; i < pipe_amount; i++){
	   // printf("%s\n", pipes[i]);
		pipe_args[i] = split(pipes[i]);
		if (pipe(pipe_fd[i]) < 0){
		    for (int j = 0; j < i ; j++){
		        close(pipe_fd[i][0]);
		        close(pipe_fd[i][1]);
		    }
		    fprintf(stderr, "pipe: fail to open pipe");
		    exit(EXIT_FAILURE);
		}
	}

	pid_t pid, wpid;
	int status;
	int saved_stdout, saved_stdin;
    saved_stdin = dup(0);
	saved_stdout = dup(1);
	for (int i = 0; i < pipe_amount; i++) {
	    if (!i){
	        
	        for (int j = 0; pipe_args[0][j]; j++){
	            if (!strcmp(pipe_args[0][j], "<")) {
	                
                    int file = open(pipe_args[0][j + 1], O_RDONLY);
	                dup2(file, STDIN_FILENO);
	                
                    close(file);
                    pipe_args[0][j] = NULL;
	            }
	        }
	        
            
	        for (int j = 0; j < builtin_amount(); j++){
        		if (!strcmp(pipe_args[0][0], builtin_name[j])) {
        		    // saved_stdout = dup(1);
        		    if (dup2(pipe_fd[0][1], STDOUT_FILENO) < 0){
            		    perror("dup2");
            		    return 1;
            		}
        			(*builtin_func[j])(pipe_args[0]);
                    
                	i++;
        			break;
        		}
        	}
        	dup2(saved_stdout, 1);
	    }
	    
	    else if (i == pipe_amount - 1){

	        for (int j = 0; pipe_args[pipe_amount - 1][j]; j++){
	            
	            if (!strcmp(pipe_args[pipe_amount - 1][j], ">")) {
	                int file = open(pipe_args[pipe_amount - 1][j + 1], O_WRONLY | O_CREAT, 0777);
                    dup2(file, STDOUT_FILENO);
                    close(file);
                    pipe_args[pipe_amount - 1][j] = NULL;
	            }
	        }
	        
	    }
	    
	   // read(pipe_fd[0][0], buffer, sizeof(buffer));
        pid = fork();
        if (pid < 0){
            for (int j = 0; j < pipe_amount ; j++){
		        close(pipe_fd[j][0]);
		        close(pipe_fd[j][1]);
		    }
		    perror("fork");
        }
    	else if (pid == 0) { // Child
    	   // printf("%d\n", i);
      	    // 讀取端
      		if (i != 0) {
        		// 用 dup2 將 pipe 讀取端取代成 stdin
        		if (dup2(pipe_fd[i - 1][0], STDIN_FILENO) < 0){
        		    
        		    perror("dup1");
        		    return 1;
        		}
      		}

      		// 用 dup2 將 pipe 寫入端取代成 stdout
      		if (i != pipe_amount - 1) {
        		if (dup2(pipe_fd[i][1], STDOUT_FILENO) < 0){
        		    perror("dup2");
        		    return 1;
        		}
      		}

      		// 關掉之前一次打開的
     	 	for (int j = 0; j < pipe_amount; j++) {
        		close(pipe_fd[j][0]);
        		close(pipe_fd[j][1]);
      		}

      		if (!pipe_args[i][0]) {
      		    fprintf(stderr, "sh: expect arguments\n");
        		return 1;
        	}
        
        // 	for (int j = 0; j < builtin_amount(); j++){
        // 		if (!strcmp(pipe_args[i][0], builtin_name[j])) {
        // 			return (*builtin_func[j])(pipe_args[i]);
        // 		}
        // 	}
        	
        	execvp(pipe_args[i][0], pipe_args[i]);
            perror("fork");
            exit(EXIT_FAILURE);
            return 0;
    	}
    //     else { // Parent
    //         // printf("- fork %d\n", pid);
    //   		if (i != 0) {
    //     		close(pipe_fd[i - 1][1]);
    //     		close(pipe_fd[i][0]);
    //   		}
    // 	}
	}
	dup2(saved_stdin, 0);
	dup2(saved_stdout, 1);
	for (int j = 0; j < pipe_amount; j++) {
        close(pipe_fd[j][0]);
        close(pipe_fd[j][1]);
    }
	
	do {
        wpid = waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
	
// 	fflush(0);
// 	fflush(1);
	return 1;
}

int execute(char ** args) {
	if (args[0] == NULL) {
	   // fprintf(stderr, "sh: expect arguments\n");
		return 1;
	}
	
	for (int j = 0; args[j]; j++){
	    if (!strcmp(args[j], "<")) {
	        
            int file = open(args[j + 1], O_RDONLY);
	        dup2(file, STDIN_FILENO);
            close(file);
            args[j] = NULL;
	    }
	    else if (!strcmp(args[j], ">")){
	        
            int file = open(args[j + 1], O_WRONLY | O_CREAT, 0777);
	        dup2(file, STDOUT_FILENO);
	                
            close(file);
            args[j] = NULL;
	    }
	}

	for (int i = 0; i < builtin_amount(); i++){
		if (!strcmp(args[0], builtin_name[i])) {
			return (*builtin_func[i])(args);
		}
	}
	
	launch(args);

}

int launch(char **args) {
    pid_t pid, wpid;
    int status;
    pid = fork();
    if (!pid) {
        if (execvp(args[0], args) == -1) {
            perror("fork");
        }
        exit(EXIT_FAILURE);
    }
    else if (pid < 0) {
        perror("fork");
    }
    else {
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

char **pipe_split(char *line) {
    
    
	int bufsize = PIPE_BUFSIZE, pos = 0;
	char **pipes = malloc(bufsize * sizeof(char *));
	char *pipe; const char s[2] = "|";

	if (!pipes) {
		fprintf(stderr, "allocation error.\n");
		exit(EXIT_FAILURE);
	}
	char *subline = malloc(strlen(line) + 1); strcpy(subline, line);
	pipe = strtok(subline, s); char *check = malloc(strlen(pipe) + 1); strcpy(check, pipe);
	check = strtok(check, " ");
	if (check && !strcmp(check, "replay")){
        char *number = strtok(NULL, " ");
        Queue *tmp = tail;
        char *lines[RECORD_BUFSIZE];
        int i, j;
        for (i = 0; tmp && i < RECORD_BUFSIZE; i++){
            lines[i] = tmp->line;
            tmp = tmp->next;
        }
        
        for (j = 1; i && j < atoi(number); j++){
            --i;
        }
        
        if (j == atoi(number) && i){
            --i;
            strtok(line, s);    char *c = strtok(NULL, s);
            if (c){
                strcpy(subline, lines[i]);
                line = strcat(subline, " |");
                line = strcat(line, c);
            }
            strcpy(tail->line, line);
            // strcpy(lines[i], subline);
            // pipe = strtok(line, s);
        }
        else{
            fprintf(stderr, "replay: wrong args”\n");
            tail = tail->next;
            return NULL;
        }
	}
	
	pipe = strtok(line, s);
	while (pipe != NULL) {
		pipes[pos] = pipe;
		pos++;

		if (pos >= bufsize) {
			bufsize += PIPE_BUFSIZE;
			pipes = realloc(pipes, sizeof(char *) * bufsize);
			if (!pipes) {
				fprintf(stderr, "allocation error.\n");
				exit(EXIT_FAILURE);
			}
		}

		pipe = strtok(NULL, s);
	}
	
	pipes[pos] = NULL;
	pipe_amount = pos;
	
// 	free(subline);
// 	free(check);
	return pipes;
}

char **split(char *line) {

    int bufsize = TOKEN_BUFSIZE, pos = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token;

    if (!tokens) {
        fprintf(stderr, "allocation error.\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOKEN_DELIM);
    while (token != NULL) {
        // printf("%s\n", token);
        tokens[pos] = token;
        pos++;

        if (pos >= bufsize) {
            bufsize += TOKEN_BUFSIZE;
            tokens = realloc(tokens, sizeof(char *) * bufsize);
            if (!tokens) {
                fprintf(stderr, "allocation error.\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOKEN_DELIM);
    }
    tokens[pos] = NULL;

    return tokens;
}

char *read_line() {
    int bufsize = LINE_BUFSIZE, pos = 0;
    char *buffer = malloc(sizeof(char) * bufsize);
    int c;

    if (!buffer) {
        fprintf(stderr, "allocation error.\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        c = getchar();

        if (c == EOF || c == '\n') {
            buffer[pos] = '\0';
            return buffer;
        }
        else {
            buffer[pos] = c;
        }
        pos++;

        if (pos >= bufsize) {
            bufsize += LINE_BUFSIZE;
            buffer = realloc(buffer, sizeof(char) * bufsize);
            if (!buffer) {
                fprintf(stderr, "allocation error.\n");
                exit(EXIT_FAILURE);
            }
        }
    }

}

void loop() {
    char *line;
    char **pipes;
    int status;
    Queue *tmp;

    do {
        printf(">>> $ ");
        line = read_line();
        
        tmp = (Queue *)malloc(sizeof(Queue));
        tmp->line = malloc(strlen(line) + 1);
        strcpy(tmp->line, line);
        tmp->next = tail;
        tail = tmp;
        
        pid_t pid, wpid;
        if (line[strlen(line) - 1] == '&'){
            pid = fork();
            if (pid < 0){
                perror("fork");
            }
            else if (pid > 0){
                printf("%d\n", pid);
                do {
                    wpid = waitpid(pid, &status, WUNTRACED);
                } while (!WIFEXITED(status) && !WIFSIGNALED(status));
                status = 1;
            }
            else{
                int saved_stdout = dup(1), saved_stdin = dup(0);
                pipes = pipe_split(strtok(line, "&"));
                status = pipe_execute(pipes);
                dup2(saved_stdin, 0);
	            dup2(saved_stdout, 1);
                free(line);
                free(pipes);
                break;
            }
        }
        else {
            int saved_stdout = dup(1), saved_stdin = dup(0);
            pipes = pipe_split(line);
            status = pipe_execute(pipes);
            dup2(saved_stdin, 0);
	        dup2(saved_stdout, 1);
            free(line);
            free(pipes);
        }
    } while (status);
}

int main() {
    printf("==============================================\n");
    printf("* Welcome to my little shell:                *\n");
    printf("*                                            *\n");
    printf("* Type \"help\" to see builtin functions.      *\n");
    printf("*                                            *\n");
    printf("* If you want to do things below:            *\n");
    printf("* + redirection: \">\" or \"<\"                  *\n");
    printf("* + pipe: \"|\"                                *\n");
    printf("* + background: \"&\"                          *\n");
    printf("* Make sure they are seperated by \"(space)\". *\n");
    printf("*                                            *\n");
    printf("* Have fun!!                                 *\n");
    printf("==============================================\n");
    loop();
    return 0;
}