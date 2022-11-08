#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

int my_errno; // for differentiating run_command() return code from execv and error

char error_message[30] = "An error has occurred\n";
void print_err() {
	write(STDERR_FILENO, error_message, strlen(error_message)); 
}

char **path_list;
size_t path_list_size;
size_t path_count;

char *find_prog(char *prog) {
	char *prog_path = NULL;
	for (int i = 0; i < path_count; i++) {
		size_t n = strlen(path_list[i]) + strlen(prog) + 4;
		char format[n];
		sprintf(format, "%s/%s", path_list[i], prog);
		if (!access(format, X_OK)) {
			prog_path = strdup(format);
			break;
		}
	}
	return prog_path;
}

int run_command(char **tokens, size_t token_count) {
	char* prog_path;
	if ( (prog_path = find_prog(tokens[0])) == NULL) {
		//fprintf(stderr, "Error: executable not found\n");
		print_err();
		my_errno = 0;
	}	
	else {
		int rc = fork();
		if (rc < 0) {
			fprintf(stderr, "Error: fork failed");
			exit(1);
		} else if (rc == 0) { // child process
			char *args[token_count+1];
			int redirect_found = 0;
			char *redirect_filepath = NULL;
			for (int i = 0; i < token_count; i++) {
				if (!strcmp(tokens[i], ">")) {
					args[i] = NULL;
					redirect_found = 1;
					if ( (++i < token_count) && strcmp(tokens[i],">") 
							&& (i == token_count-1)) {
						redirect_filepath = tokens[i];
						break;
					}
					else {
						print_err();
						my_errno = 0;
						exit(0);
					}	
				}
				args[i] = tokens[i];
			}

			if (!redirect_found) args[token_count] = NULL;

			if (redirect_found) {
				int file_desc;
				if ( (file_desc = open(redirect_filepath, O_WRONLY | O_TRUNC| O_CREAT, 0666)) < 0) {
					fprintf(stderr,"Error: open call failed.\n");
					exit(1);
				}
				dup2(file_desc, 1);
				dup2(file_desc, 2);
				close(file_desc);
			}
			execv(prog_path, args);
		} else {
			int exit_stat;
			wait(&exit_stat);
			if (WIFEXITED(exit_stat)) {
				free(prog_path);
				prog_path = NULL;
				return WEXITSTATUS(exit_stat);
			} else {
				fprintf(stderr, "Error: execv failed.\n");
				exit(1);
			}
		}
	}
	return 0;
}

/* Checks size of arr and reallocs if needed */
char **check_size(char **arr, size_t *arr_size, size_t elem_count) {
	if (arr == NULL) return NULL;
	if (elem_count > *(arr_size)) {
		size_t new_arr_size = *(arr_size)*2;
		if ( (arr = realloc(arr, new_arr_size)) == NULL) {
			fprintf(stderr, "Error: realloc failed.\n");
			exit(1);
		}
		*(arr_size) = new_arr_size;
	}
	return arr;
}

int do_built_in(char **tokens, size_t token_count) {
	if (token_count == 0) return 0;
	
	if (!strcmp(tokens[0], "exit")) {
		if (token_count > 1) print_err(); 
		exit(0);
	}

	if (!strcmp(tokens[0], "cd")) {
		if (token_count != 2) print_err();
		else if (chdir(tokens[1]) < 0) print_err();
		return 1;
	}

	if (!strcmp(tokens[0], "path")) {
		if (token_count > 1) {
			if ( (token_count - 1) > path_list_size) 
				path_list = (char**)realloc(path_list, path_list_size*2);
			else {
				path_count = token_count - 1;
				for (int i = 0; i < (path_count); i++) {
					path_list[i] = tokens[i+1];
				}
			}	
		} else {
			path_count = 0;
		}
		return 1;
	}

	return 0;
}

int do_if(char **tokens, size_t token_count, int *i) {
	int fi = 0;
	size_t size_args = sizeof(char*)*8;
	char **args = malloc(size_args);

	int eq = 0;
	int neq = 0;
	size_t arg_c = 0;
	// parse COMMAND and break on == or !=
	while (*(i) < token_count)	{
		if (!strcmp(tokens[*(i)], "==")) {
			eq = 1;
			(*i)++;
			break;
		}
		if (!strcmp(tokens[*(i)], "!=")) {
			neq = 1;
			(*i)++;
			break;
		}
		args = check_size(args, &size_args, arg_c);
		args[arg_c++] = tokens[(*i)++];
	}
	args = check_size(args, &size_args, arg_c);
	args[arg_c] = NULL;
	if (!(eq || neq)) goto err;
	if (!(*(i) < token_count)) goto err;
	int res = run_command(args, arg_c);
	char *ptr;
	int constant = strtol(tokens[(*i)++], &ptr, 10);
	if (!my_errno || ptr == NULL) goto err;
	int eval = 0;
	if (eq && (res == constant)) {
		eval = 1;
	} else if (neq && (res != constant)) {
		eval = 1;
	}
	if (!(*(i) < token_count)) goto err;
	if (strcmp(tokens[(*i)++], "then")) goto err;

	// parse rhs command and look for fi
	int cmd_executed = 0;
	if (!strcmp(tokens[*(i)], "if")) {
		(*i)++;
		if (!do_if(tokens, token_count, i)) { // some syntax error occured
			goto err;
		}
		cmd_executed = 1;
	}
	arg_c = 0; // reset arg_c to write over old tokens
	while (*(i) < token_count) {
		if (!strcmp(tokens[*(i)], "fi")) {
			if (!cmd_executed && !arg_c) {
				goto ret;
			}
			fi = 1; // fi found
			(*i)++;
			break;
		}	
		args = check_size(args, &size_args, arg_c);
		args[arg_c++] = tokens[(*i)++];
	}
	if (!fi || ( (*i < token_count) && strcmp(tokens[*i], "fi")) ) {
		goto err;
	}
	if (!arg_c) goto ret; // last fi reached
	args = check_size(args, &size_args, arg_c);
	args[arg_c] = NULL;
	if (!eval) {
		goto ret;
	}
	if (do_built_in(args, arg_c)) {
		goto ret;
	} else {
		run_command(args, arg_c);
	}
	goto ret;
err:
	free(args);
	args = NULL;
	return 0;
ret:	
	free(args);
	args = NULL;
	return 1;	
}

int main(int argc, char *argv[]) {
	
	path_list_size = sizeof(char*)*4;
	path_list = malloc(path_list_size);
	path_list[0] = "/bin";
	path_count = 1;

	int batch_mode = 0;
	FILE *fp;
	FILE *istream = stdin;
	
	if (argc > 2) {
		//fprintf(stderr, "usage: ./wish [batch_file]\n");
		print_err();
		exit(1);
	}

	if (argc > 1) {
		batch_mode = 1;
		if ( (fp = fopen(argv[1],"r")) == NULL) {
			//fprintf(stderr,"Error: bad batch file.\n");
			print_err();
			exit(1);
		}
		istream = fp;
	}

	size_t line_size = 16;
	char *lineptr = malloc(line_size);

	while(1) {

		my_errno = 1;

		if (!batch_mode) printf("goksh> ");
		int chars_read = getline(&lineptr, &line_size, istream);
		
		// check for EOF
		if (chars_read < 0) { 
			exit(0);
		}

		// parse cmd and args
		char **tokens = malloc(sizeof(char*)*8);
		if (tokens == NULL) { // check if malloc failed
			fprintf(stderr, "Error: malloc failed");
			exit(1);
		}

		size_t tokens_size = sizeof(char*)*8;
		size_t token_count = 0;
		char *token;
		int continue_outer = 0;

		while ( (token = strsep(&lineptr," ")) != NULL) {
			token[strcspn(token, "\n")] = '\0'; // remove newline characters
			token[strcspn(token, "\t")] = '\0'; // remove tab characters
			
			if (!strcmp(token,"")) continue;
			
			size_t token_length = strlen(token);
			if (token_length > 1) {
				char token_arr[token_length+1];
				strcpy(token_arr, token);
				int ptr = -1;
				for (int i = 0; i < token_length; i++) {
					if (token_arr[i] == '>') {
						ptr = i;
						break;
					}	
				}
				if (ptr != -1) {		
					for (int i = ptr+1; i < token_length; i++) { 
						if (token_arr[i] == '>') { // catch multiple > syntax error
							continue_outer = 1;
							break;
						}
					}

					if (continue_outer) break;

					char *left = strsep(&token, ">");
					if (strcmp(left, "")) {
						tokens = check_size(tokens, &tokens_size, token_count);
						tokens[token_count++] = left;
					}
					tokens = check_size(tokens, &tokens_size, token_count);
					tokens[token_count++] = ">";
					char *right = strsep(&token, ">");
					if (strcmp(right, "")) {
						tokens = check_size(tokens, &tokens_size, token_count);
						tokens[token_count++] = right;
					}
					if ((token = strsep(&lineptr," ")) != NULL) continue_outer = 1; // multiple args after >
					break;
				}
			}	
			
			tokens = check_size(tokens, &tokens_size, token_count);
			
			tokens[token_count] = token;
			token_count++;	

		}

		if (continue_outer) {
			print_err();
			free(tokens);
			tokens = NULL;
			continue;
		}
		
		if (token_count == 0);
		else if (!strcmp(tokens[0], "exit")) {
			if (token_count > 1) print_err(); 
			exit(0);
		} else if (!strcmp(tokens[0], "cd")) {
			if (token_count != 2) print_err();
			else if (chdir(tokens[1]) < 0) print_err();
		} else if (!strcmp(tokens[0], "path")) {
			if (token_count > 1) {
				if ( (token_count - 1) > path_list_size) 
					path_list = (char**)realloc(path_list, path_list_size*2);
			    else {
					path_count = token_count - 1;
					for (int i = 0; i < (path_count); i++) {
						path_list[i] = tokens[i+1];
					}
				}	
			} else {
				path_count = 0;
			}
		} else if (!strcmp(tokens[0], "if")) {
			int i = 1;
			if (!do_if(tokens, token_count, &i)) print_err();
		} else {
			run_command(tokens, token_count);
		}

		free(tokens);
		tokens = NULL;	
	}

	free(lineptr);
	lineptr = NULL;
	 	
	if (batch_mode) fclose(fp);
	
	free(path_list);
	path_list = NULL;

	return 0;
}

