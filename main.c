#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>


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
	FILE *file;
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
int find_string(char *search, int start_line, int is_tag, struct state_spec *state);
int substitute(char *replace, char *find, int start, int end, char mode, int num, struct state_spec *state);
char convert_esc(char c);
int next_char(char *c, int convert, int echo, struct state_spec *state);
void **replace_elements_in_vector(void **dest, int *dest_length, void **src, int src_length, int pos, int num);
void add_char_to_buffer(char **line, int *length, int *buf_size, char c, int unlimited, int echo);
char get_flags(struct command_spec *command);
void get_buffer_name(struct command_spec *command);
void get_string(char **str, int *length, char delim, int full, int unlimited, int literal, struct state_spec *state);
char **get_lines(int *length, int literal, struct state_spec *state);
struct command_spec* get_command(struct state_spec *state);
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
	state->file = NULL;
	do
	{
		command = get_command(state);
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
int find_string(char *search, int start_line, int is_tag, struct state_spec *state)
{
	int i;
	char *found;
	int length = strlen(search);
	char next;
	for(i = start_line; i <= state->dollar; i++)
	{
		found = strstr(state->main_buffer[i], search);
		if(is_tag)
		{
			next = found?found[length]:'0';
			if(found == state->main_buffer[i] && next && !isalnum(next))
				return i;
		}
		else if(found)
			return i;
	}
	for(i = 1; i < start_line; i++)
	{
		found = strstr(state->main_buffer[i], search);
		if(is_tag)
		{
			next = found?found[length]:'0';
			if(found == state->main_buffer[i] && next && !isalnum(next))
				return i;
		}
		else if(found)
			return i;
	}
	return 0;
}
int substitute(char *replace, char *find, int start, int end, char mode, int num, struct state_spec *state)
{
	int old_str_length, replace_length, find_length, new_str_length = 0, done = 0, line, num_subs = 0;
	replace_length = strlen(replace);
	find_length = strlen(find);
	for(line = start; line <= end; line++)
	{
		char *found;
		char *old_str = state->main_buffer[line];
		old_str_length = strlen(old_str);
		int made_sub = 0, start_from = 0;
		while((found = strstr(old_str+start_from, find)))
		{
			char *new_str;
			int pos = found-old_str;
			start_from = pos + replace_length;
			if(num >= 0 &&num_subs >= num)
				return num_subs;
			new_str = malloc(old_str_length - find_length + replace_length);
			new_str[0] = '\0';
			strncat(new_str, old_str, pos);
			if(mode == 'W' || mode == 'V')
			{
				char c, lastchar = '0';
				int skip = 0;
				printf("%s\"%s\"%s\r", new_str, find, found+find_length);
				do
				{
					next_char(&c, 1, 1, state);
					if(isdigit(c))
					{
						if(isdigit(lastchar))
						{
							num = num * 10 + c - '0';
						}
						else if(lastchar == ':')
							num = c - '0';
						else
						{
							skip = 1;
							break;
						}
					}
					else if(c == 'G' || c == 'W' || c == 'L' || c == 'V')
					{
						if(lastchar == ':')
							mode = c;
						else
						{
							skip = 1;
							break;
						}
					}
					else if(c == 'S')
						break;
					else if(!isblank(c) && c != ':')
					{
						skip = 1;
						break;
					}
					lastchar = c;
				} while(1);
				printf("\r\n");
				if(skip)
				{
					free(new_str);
					continue;
				}
			}
			strcat(new_str, replace);
			strcat(new_str, found+find_length);
			free(old_str);
			state->main_buffer[line] = old_str = new_str;
			num_subs++;
			made_sub = 1;
		}
		if(made_sub && (mode == 'L' || mode == 'V'))
		{
			printf("%s\r", state->main_buffer[line]);
		}
	}
	return num_subs;
}
char convert_esc(char c)
/* Converts one or more characters for response and printing.  Capitalizes and turns relevant multi-character escape sequences into single command characters */
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
int next_char(char *c, int convert, int echo, struct state_spec *state)
{
	int status;
	if(state->file)
	{
		status = fscanf(state->file, "%c", c);
	}
	else
	{
		status = scanf("%c", c);
	}
	if(!status || status == EOF)
		return 0;
	if(convert)
		*c = convert_esc(*c);
	if(echo)
		printf("%c", *c);
	return status;
}
void **replace_elements_in_vector(void **dest, int *dest_length, void **src, int src_length, int pos, int num)
/* Inserts all of the items from src into dest at position pos, replacing num of dest's existing items. All replaced items are freed. Dest keeps the items from src and src itself is freed. dest_length should point to dest's length, and this will be set to the length of the new dest. The new dest is returned. */
{
	int i, new_length;
	new_length = *dest_length - num + src_length;
	if(num < src_length)
	{
		dest = realloc(dest, new_length * sizeof(void**));
		for(i = *dest_length-1; i >= pos+num; i--)
		{
			dest[i+new_length-*dest_length] = dest[i];
		}
	}
	else if(num > src_length)
	{
		for(i = pos+src_length; i < new_length; i++)
		{
			if(i < pos+num)
				free(dest[i]);
			dest[i] = dest[i+*dest_length-new_length];
		}
		dest = realloc(dest, new_length * sizeof(void**));
	}
	for(i = 0; i < src_length; i++)
	{
		if(i < num)
			free(dest[i+pos]);
		dest[i+pos] = src[i];
	}
	free(src);
	*dest_length = new_length;
	return dest;
}
void add_char_to_buffer(char **line, int *length, int *buf_size, char c, int unlimited, int echo)
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
			if(c != 0x04 && echo)
				printf("%c", c);
		}
	}
	else
	{
		(*line)[*length] = c;
		*length = *length+1;
		if(echo)
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
void get_string(char **str, int *length, char delim, int full, int unlimited, int literal, struct state_spec *state)
{
	char c;
	int newstr = 1;
	int stop = 0;
	int buf_size = BUF_INCREMENT;
	int my_length;
	int status;
	if(!length) {length = &my_length;}
	*str = malloc(buf_size+1);
	*length = 0;
	do
	{
		status = next_char(&c, 0, 0, state);
		if(!status)
		{
			stop = 1;
			add_char_to_buffer(str, length, &buf_size, 0x04, unlimited, !literal);
		}
		else if(literal)
		{
			add_char_to_buffer(str, length, &buf_size, c, unlimited, !literal);
			stop = (c == '\n');
		}
		else
		{
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
					status = next_char(&c, 0, 0, state);
					add_char_to_buffer(str, length, &buf_size, c, unlimited, !literal);
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
						add_char_to_buffer(str, length, &buf_size, c, unlimited, !literal);
					}
					else
					{
						add_char_to_buffer(str, length, &buf_size, c, unlimited, !literal);
					}
			}
		}
	} while(!stop);
	(*str)[*length] = '\0';
	*length = *length+1;
}
char **get_lines(int *length, int literal, struct state_spec *state)
{
	char **input_lines = NULL;
	char *buffer = NULL;
	char **one_line = NULL;
	int buffer_length = 0;
	int done = 0;
	*length = 0;
	do {
		get_string(&buffer, &buffer_length, '\0', 1, 1, literal, state);
		if(buffer[buffer_length-2] == 0x04)
			done = 1;
		if(buffer[0] != 0x04)
		{
			buffer[buffer_length-2] = '\n';
			one_line = malloc(sizeof(void*));
			*one_line = buffer;
			input_lines = (char**)replace_elements_in_vector((void**)(input_lines), length, (void**)one_line, 1, *length, 0);
		}
		else
			free(buffer);
	} while(!done);
	return input_lines;
}
struct command_spec* get_command(struct state_spec *state)
{
	unsigned char c = '\0';
	int done = 0;
	char *cmd_char_ptr = NULL;
	int compound_valid = 0;	/* Whether a comma, plus, minus, or space would be valid now */
	int second_addr = 0;	/* Whether we are currently reading a second address (i.e. it's after the comma) */
	int cmd_valid = 1;	/* Whether a command would be valid now */
	int rel_valid = 1;	/* Whether a relative (. or $) would be valid (because one hasn't been used yet) */
	int rubout_pressed = 0;
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
		if(c == 0x7F)
		{
			if(rubout_pressed)
			{
				return NULL;
			}
			else
			{
				printf("%c", 0x07);
				rubout_pressed = 1;
			}
		}
		else
		{
			rubout_pressed = 0;
		}
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
				get_string(&((*line)->search),&((*line)->line), c=='['?']':c, 0, c=='[', 0, state);
			}
			else if((*line)->type == 'c')
			{
				(*line)->type = c;
				get_string(&((*line)->search),&((*line)->line), c=='['?']':c, 0, c=='[', 0, state);
			}
			else
			{
				line = &((*line)->next);
				*line = new_line_spec('+',c,0,NULL);
				get_string(&((*line)->search),&((*line)->line), c=='['?']':c, 0, c=='[', 0, state);
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
					get_string(&(command->arg1), NULL, c, 0, 1, 0, state);
				}
				else if(c == 'S')
				{
					c = get_flags(command);
					if(!c)
					{
						free_command_spec(command);
						return NULL;
					}
					get_string(&(command->arg1), NULL, c, 0, 1, 0, state);
					printf(" FOR %c", c);
					get_string(&(command->arg2), NULL, c, 0, 1, 0, state);
					if(strlen(command->arg2) == 0)
					{
						free_command_spec(command);
						return NULL;
					}
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
	int line_number = 0, first = 1;
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
			case ':':
			if(!(line_number = find_string(line->search, first?state->dot+1:line_number, 1, state))) {return -1;}
			break;
			case '[':
			if(!(line_number = find_string(line->search, first?state->dot+1:line_number, 0, state))) {return -1;}
			break;
			default:
			return -1;
		}
		line = line->next;
		first = 0;
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
	if(line1 == 0 && command->command != 'A' && command->command != '=' && command->command != 'R')
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
	if(line2 == 0 && command->command != 'A' && command->command != '=' && command->command != 'R')
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
		int i, n, length, input_length, buffer_length, done;
		char c;
		char *buffer;
		char **input_lines, **one_line;
	case '^':
		if(state->dot <= 1)
		{
			err();
			return 0;
		}
		state->dot = state->dot - 1;
		printf("%s\r", state->main_buffer[state->dot]);
		break;
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
			printf("%s%s", state->main_buffer[i], sep);
		}
		state->dot = line2;
		break;
	case 'I':
		if(command->start == NULL)
			line1 = state->dot;
		line1--;
		/* Intentional Fallthrough */
	case 'A':
		done = 0;
		if(!command->start && command->command != 'I')
			line1 = state->dollar;
		do {
			get_string(&buffer, &length, '\0', 1, 1, 0, state);
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
	case 'D':
		length = state->dollar+1;
		state->main_buffer = (char**)replace_elements_in_vector((void**)(state->main_buffer), &(length), NULL, 0, line1, line2-line1+1);
		state->dollar = length-1;
		state->dot = line1-1;
		break;
	case 'C':
		input_lines = get_lines(&input_length, 0, state);
		length = state->dollar+1;
		state->main_buffer = (char**)replace_elements_in_vector((void**)(state->main_buffer), &(length), (void**)input_lines, input_length, line1, line2-line1+1);
		state->dollar = length-1;
		state->dot = line1 + input_length - 1;
		break;
	case 'R':
		if(!(state->file = fopen(command->arg1, "r")))
		{
			err();
			return 0;
		}
		if(!command->start)
			line1 = state->dollar;
		line1++;
		input_lines = get_lines(&input_length, 1, state);
		length = state->dollar+1;
		state->main_buffer = (char**)replace_elements_in_vector((void**)(state->main_buffer), &(length), (void**)input_lines, input_length, line1, 0);
		state->dollar = length-1;
		state->dot = line1 + input_length - 1;
		fclose(state->file);
		state->file = NULL;
		break;
	case 'W':
		if(!(state->file = fopen(command->arg1, "w")))
		{
			err();
			return 0;
		}
		if(!(command->start || command->end))
		{
			line1 = 1;
			line2 = state->dollar;
		}
		for(i = line1; i <= line2; i++)
		{
			fprintf(state->file, "%s", state->main_buffer[i]);
		}
		fclose(state->file);
		state->file = NULL;
		break;
	case 'S':
		n = substitute(command->arg1, command->arg2, line1, line2, command->flag, command->num, state);
		if(n == 0)
			err();
		else
			printf("%i\r\n", n);
		break;
	case 'F':
			return 1;
	default:
		printf("[not implemented yet]\r\n");
	}
	return 0;
}
