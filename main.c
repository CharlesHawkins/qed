#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>


const char up_arrow[4] = {0xE2, 0x86, 0x91, 0x00};
const char left_arrow[4] = {0xE2, 0x86, 0x90, 0x00};
const char *cmd_chars = "\"/=^<\n\rABCDEFGIJKLMPQRSTVW";
char *cmd_strings[26] = {"\"", "/", "=", "↑", "←", "\r\n", "\r\n", "APPEND", "BUFFER #", "CHANGE", "DELETE", "EDIT", "FINISHED", "GET #", "INSERT", "JAM INTO #", "KILL #", "LOAD #", "MODIFY", "PRINT", "QUICK", "READ FROM ", "SUBSTITUTE ", "TABS", "VERBOSE", "WRITE ON "};
const int cmd_addrs[26] = {0, 2, 1, 0, 1, 2, 2, 1, 0, 2, 2, 2, 0, 2, 1, 0, 0, 2, 2, 2, 0, 1, 2, 0, 0, 2};
const char *cmd_noconf = "\"/=^<\n\r";
const char *cmd_noaddr = "\"BFJKQTV";
const int BUF_INCREMENT = 30;

struct state_spec {
	char **main_buffer;
	int dot;
	int dollar;
};
struct command_spec {
	struct line_spec *start;
	struct line_spec *end;
	char command;
	char *arg1;
	char *arg2;
	char flag;
	int num;
};

struct line_spec {
	char sign;
	char type;
	int line;
	char *search;
	struct line_spec *next;
};
void err();
char convert_esc(char c);
void add_char_to_buffer(char **line, int *length, int *buf_size, char c, int unlimited);
char get_flags(struct command_spec *command);
void get_buffer_name(struct command_spec *command);
void get_string(char **str, int *length, char delim, int full, int unlimited);
struct command_spec* qed_getline();
int resolve_line_spec(struct line_spec *line, struct state_spec *state);
int execute_command(struct command_spec *command, struct state_spec *state);
int increase_buffer(char **buffer, size_t *size);
struct line_spec *new_line_spec(char sign, char type, int line, char *search);
void free_line_spec(struct line_spec *ls);
void free_command_spec(struct command_spec *cmd);
int main(int argc, char **argv)
{
	struct command_spec *command;
	struct state_spec *state;
	struct line_spec *ls;
	int finished = 0;
	struct termios qed_term_settings;
	struct termios original_term_settings;
	tcgetattr(fileno(stdin), &original_term_settings);
	qed_term_settings = original_term_settings;
	cfmakeraw(&qed_term_settings);
	tcsetattr(fileno(stdin), TCSANOW, &qed_term_settings);
	setbuf(stdout,NULL);

	state = malloc(sizeof(struct state_spec));
	state->main_buffer = calloc(sizeof(int*), 1);
	state->dollar = 0;
	state->dot = 0;
	do
	{
		command = qed_getline();
		if(command != NULL)
		{
			finished = execute_command(command, state);
			free_command_spec(command);
		}
		else
		{
			err();
			printf("\r\n");
		}
	} while(!finished);

	tcsetattr(fileno(stdin), TCSANOW, &original_term_settings);
	return 0;
}

void err()
{
	printf("?\r\n");
}
char convert_esc(char c)
{
	if(c >= 'a' && c <= 'z')
	{
		c += 'A'-'a';
	}
	else if(c == '\r')
	{
		c = '\n';
	}
	else if(c == 0x1B)
	{
		char c;
		scanf("%c", &c);
		if(c == 0x5B)
		{
			scanf("%c", &c);
			switch(c)
			{
				case 'A': c = '^'; break;
				case 'D': c = '<'; break;
				default: c = 0x01;
			}
		}
		else
		{
			c = 0x01;
		}
	}
	return c;
}
void free_line_spec(struct line_spec *ls)
/* Frees the line spec ls and recursively frees any dependent line specs */
{
	if(ls->next)
	{
		free_line_spec(ls->next);
	}
	if(ls->search)
	{
		free(ls->search);
	}
	free(ls);
}
struct line_spec *new_line_spec(char sign, char type, int line, char *search)
{
	struct line_spec *ls = malloc(sizeof(struct line_spec));
	ls->sign = sign;
	ls->type = type;
	ls->line=line;
	ls->search = search;
	ls->next = NULL;
	return ls;
}
void free_command_spec(struct command_spec *cmd)
/* Frees the command_spec cmd and its dependent line_specs, if any */
{
	if(cmd->start)
	{
		free_line_spec(cmd->start);
	}
	if(cmd->end)
	{
		free_line_spec(cmd->end);
	}
	if(cmd->arg1)
	{
		free(cmd->arg1);
	}
	if(cmd->arg2)
	{
		free(cmd->arg2);
	}
	free(cmd);
}
int increase_buffer(char **buffer, size_t *size)
{
	int old_size = *size;
	int new_size = old_size + BUF_INCREMENT;
	char *old_buffer = *buffer;
	char *new_buffer = malloc(new_size);
	strncpy(new_buffer, old_buffer, old_size);
	*size = new_size;
	*buffer = new_buffer;
	free(old_buffer);
	return new_buffer != NULL;
}
void add_char_to_buffer(char **line, int *length, int *buf_size, char c, int unlimited)
/* Adds the character c to the buffer pointed to by line whose current number of characters is pointed to by length and whose currently-allocated space is pointed to by buf_size.  If unlimited is true, the buffer will be reallocated if needed to make room for the new character; otherwise characters past the end are silently dropped. */
{
	if(*length == *buf_size)
	{
		if(unlimited)
		{
			*buf_size += BUF_INCREMENT;
			*line = realloc(*line, *buf_size);
			(*line)[*length] = c;
			*length = *length+1;
			if(c != 0x04)
				printf("%c", c);
		}
	}
	else
	{
		(*line)[*length] = c;
		*length = *length+1;
		printf("%c", c);
	}
}
char get_flags(struct command_spec *command)
{
	char c;
	do{
		scanf("%c",&c);
		c = convert_esc(c);
		printf("%c",c);
	} while(c == ' ' || c == '\t');
	while(c == ':')
	{
		int n = 0;
		int digit = 0;
		scanf("%c",&c);
		c = convert_esc(c);
		while(c >= '0' && c <= '9')
		{
			digit = 1;
			printf("%c",c);
			n = n * 10 + c - '0';
			command->num = n;
			scanf("%c",&c);
			c = convert_esc(c);
		}
		if(!digit)
		{
			if(c == 'G' || c == 'W' || c == 'L' || c == 'V')
			{
				printf("%c",c);
				command->flag = c;
				do{
					scanf("%c",&c);
					c = convert_esc(c);
					printf("%c",c);
				} while(c == ' ' || c == '\t');
			}
			else
			{
				return '\0';
			}
		}
		else
		{
			printf("%c",c);
		}
	}
	return c;
}
void get_buffer_name(struct command_spec *command)
{
	char c;
	scanf("%c",&c);
	c = convert_esc(c);
	if((c>= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))
	{
		printf("%c",c);
		command->arg1 = malloc(2);
		command->arg1[0] = c;
		command->arg1[1] = '\0';
	}
}
void get_string(char **str, int *length, char delim, int full, int unlimited)
{
	char c;
	int newstr = 1;
	int stop = 0;
	int buf_size = BUF_INCREMENT;
	int my_length;
	if(!length) {length = &my_length;}
	*str = malloc(buf_size+1);
	*length = 0;
	do
	{
		scanf("%c", &c);
		switch(c)
		{
			case 0x01:	/* Ctrl-A (Delete Character) */
				if(*length)
				{
					*length = *length-1;
				}
				printf("%s", up_arrow);
				if(!(*length))
				{
					printf("\r\n");
				}
				break;
			case 0x17:	/* Ctrl-W (Delete Word) */
				printf("\\");
				while((*length)&&((*str)[*length-1] == ' '||(*str)[*length-1] == '\t'))
				{
					*length = *length-1;
				}
				while((*length)&&((*str)[*length-1] != ' '&&(*str)[*length-1] != '\t'))
				{
					*length = *length-1;
				}
				if(!(*length))
				{
					printf("\r\n");
				}
				break;
			case 0x11:	/* Ctrl-Q (Delete Line) */
				printf("%s\r\n",left_arrow);
				*length = 0;
				break;
			case 0x16:	/* Ctrl-V (Literal Character) */
				scanf("%c",&c);
				add_char_to_buffer(str, length, &buf_size, c, unlimited);
				break;
			default:
				if(c == delim)
				{
					printf("%c",c);
					stop = 1;
				}
				else if(full)
				{
					if(c == 0x04)
					{
						printf("\r\n");
						stop = 1;
					}
					else if(c == '\r')
					{
						printf("\n");
						stop = 1;
					}
					fflush(stdin);
					add_char_to_buffer(str, length, &buf_size, c, unlimited);
					fflush(stdin);
				}
				else
				{
					add_char_to_buffer(str, length, &buf_size, c, unlimited);
				}
		}
	} while(!stop);
	(*str)[*length] = '\0';
	*length = *length+1;
}
struct command_spec* qed_getline()
{
	unsigned char c = '\0';
	int done = 0;
	char *cmd_char_ptr = NULL;
	int compound_valid = 0;	/* Whether a comma, plus, minus, or space would be valid now */
	int second_addr = 0;	/* Whether we are currently reading a second address (i.e. it's after the comma) */
	int cmd_valid = 1;	/* Whether a command would be valid now */
	int rel_valid = 1;	/* Whether a relative (. or $) would be valid (because one hasn't been used yet) */
	struct command_spec *command;
	struct line_spec **line;
	command = malloc(sizeof(struct command_spec));
	command->start = NULL;
	command->end = NULL;
	command->arg1 = NULL;
	command->arg2 = NULL;
	command->flag = 'G';
	command->num = -1;
	line = &(command->start);
	printf("*");
	do
	{
		if(!scanf("%c", &c))
		/* Input stream has closed! Complain and exit. */
		{
			err();
			exit(1);
		}
		c = convert_esc(c);
		if(c >= '0' && c <= '9')
		/* Received a numeric digit */
		{
			compound_valid = 1;
			cmd_valid = 1;
			if(*line == NULL)
			{
				*line = new_line_spec('+','n',c-'0',NULL);
			}
			else if((*line)->type == 'c')
			{
				(*line)->type = 'n';
				(*line)->line = c-'0';
			}
			else if((*line)->type == 'n')
			{
				(*line)->line = (*line)->line * 10 + c-'0';
			}
			else
			{
				free_command_spec(command);
				return NULL;
			}
			printf("%c",c);
		}
		else if(c == '+' || c == '-')
		{
			if(*line == NULL || !compound_valid)
			{
				free_command_spec(command);
				return NULL;
			}
			line = &((*line)->next);
			*line = new_line_spec(c,'c',0,NULL);
			cmd_valid = 0;
			compound_valid = 0;
			printf("%c",c);
		}
		else if(c == '.' || c == '$')
		{
			cmd_valid = 1;
			compound_valid = 1;
			if(rel_valid == 0)
			{
				free_command_spec(command);
				return NULL;
			}
			if(*line == NULL)
			{
				*line = new_line_spec('+',c,0,NULL);
			}
			else if((*line)->type == 'c')
			{
				(*line)->type = c;
			}
			else
			{
				free_command_spec(command);
				return NULL;
			}
			rel_valid = 0;
			printf("%c",c);
		}
		else if(c == ':' || c == '[')
		{
			printf("%c",c);
			if(*line == NULL)
			{
				*line = new_line_spec('+',c,0,NULL);
				get_string(&((*line)->search),&((*line)->line), c=='['?']':c, 0, c=='[');
			}
			else if((*line)->type == 'c')
			{
				(*line)->type = c;
				get_string(&((*line)->search),&((*line)->line), c=='['?']':c, 0, c=='[');
			}
			else
			{
				line = &((*line)->next);
				*line = new_line_spec('+',c,0,NULL);
				get_string(&((*line)->search),&((*line)->line), c=='['?']':c, 0, c=='[');
			}
			rel_valid = 0;
			compound_valid = 1;
			cmd_valid = 1;
		}
		else if(c == ',')
		{
			if(*line == NULL || !compound_valid || second_addr)
			{
				free_command_spec(command);
				return NULL;
			}
			line = &(command->end);
			cmd_valid = 0;
			compound_valid = 0;
			second_addr = 1;
			rel_valid = 1;
			printf("%c",c);
		}
		else if((cmd_char_ptr = strchr(cmd_chars, c)))
		/* Received a command character */
		{
			int cmd_char_index;
			char *cmd_str;
			if(!cmd_valid)
			{
				free_command_spec(command);
				return NULL;
			}
			cmd_char_index = (int)(cmd_char_ptr - cmd_chars);
			if((command->start != NULL) + (command->end != NULL) > cmd_addrs[cmd_char_index])
			/* Too many address lines for command */
			{
				free_command_spec(command);
				return NULL;
			}
			cmd_str = cmd_strings[cmd_char_index];
	 		printf("%s", cmd_str);
			done = 1;
			command->command = c;
			if(!strchr(cmd_noconf, c))
			{
				if(c == 'B' || c == 'G' || c == 'J' || c == 'K' || c == 'L')
				{
					get_buffer_name(command);
					if(!command->arg1)
					{
						free_command_spec(command);
						return NULL;
					}
				}
				if(c == 'R' || c == 'W')
				{
					do
					{
						scanf("%c",&c);
						printf("%c",c);
						if(c == '\n') {printf("\r");}
					} while(c == ' ' || c == '\t' || c == '\n');
					get_string(&(command->arg1), NULL, c, 0, 1);
				}
				else if(c == 'S')
				{
					c = get_flags(command);
					if(!c)
					{
						free_command_spec(command);
						return NULL;
					}
					get_string(&(command->arg1), NULL, c, 0, 1);
					printf(" FOR %c", c);
					get_string(&(command->arg2), NULL, c, 0, 1);
				}
				scanf("%c",&c);
				c = convert_esc(c);
				if(c != '.')
				{
					free_command_spec(command);
					return NULL;
				}
				printf("%c", c);
			}
		}
		else {printf("%c", 0x07);}
	} while(!done);
	return command;
}
int resolve_line_spec(struct line_spec *line, struct state_spec *state)
{
	int line_number = 0;
	if(!state || !(state->main_buffer))
	{
		return -1;
	}
	while(line)
	{
		switch(line->type)
		{
			case 'n':
			line_number = line->sign == '-'?line_number-line->line:line_number+line->line;
			break;

			case '.':
			line_number += state->dot;
			break;
			case '$':
			line_number += state->dollar;
			break;
			default:
			return -1;
		}
		line = line->next;
	}
	if(line_number < 0)
		return 1;
	else if(line_number > state->dollar)
		return -1;
	else
		return line_number;
}
int execute_command(struct command_spec *command, struct state_spec *state)
{
	int line1 = state->dot, line2 = state->dot;
	char *sep = "\r";
	if(command->start)
	{
		line1 = resolve_line_spec(command->start, state);
		if(line1 == -1)
		{
			err();
			return 0;
		}
	}
	if(line1 == 0 && command->command != 'A' && command->command != '=')
		line1 = 1;
	line2 = line1;
	if(command->end)
	{
		line2 = resolve_line_spec(command->end, state);
		if(line2 == -1)
		{
			err();
			return 0;
		}
	}
	if(line2 == 0 && command->command != 'A' && command->command != '=')
		line2 = 1;
 	if(command->command == '\n' && !command->start) 
		line2 = ++line1;
	if(!strchr(cmd_noaddr, command->command) && (line2 < line1 || line2 > state->dollar))
	{
		err();
		return 0;
	}
	if(command->command != '=' && command->command != '<' && command->command != '\n')
		printf("\r\n");
	switch(command->command)
	{
		int i, length, done = 0;
		char c;
		char *buffer;
	case '=':
		printf("%i\r\n", line1);
		break;
	case 'P':
		printf("\r\nDOUBLE? ");
		scanf("%c", &c);
		c = convert_esc(c);
		printf("%c\r\n",c);
		if(c == 'Y')
			sep = "\r\n\r";
		else if(c == 'N')
			sep = "\r";
		else
		{
			err();
			return 0;
		}
	/* Intentional fallthrough */
	case '/':
	case '\n':
		for(i=line1; i<=line2; i++)
		{
			printf("%i: %s%s", i, state->main_buffer[i], sep);
		}
		state->dot = line2;
		break;
	case 'I':
		line1--;
		/* Intentional Fallthrough */
	case 'A':
		done = 0;
		do {
			get_string(&buffer, &length, '\0', 1, 1);
			if(buffer[length-2] == 0x04)
				done = 1;
			if(buffer[0] != 0x04)
			{
				state->dot = ++line1;
				buffer[length-2] = '\n';
				state->main_buffer = realloc(state->main_buffer, sizeof(int*) * (state->dollar+2));
				for(i = state->dollar; i>=line1; i--)
				{
					state->main_buffer[i+1] = state->main_buffer[i];
				}
				state->main_buffer[line1] = buffer;
				state->dollar = state->dollar+1;
			}
			else
				free(buffer);
		} while(!done);
		break;
	case 'F':
			return 1;
	default:
		printf("[not implemented yet]\r\n");
	}
	return 0;
}
