/* QED Text Editor
 * Based on the editor of the same name by L. Peter Deutsch and Butler W. Lampson.
 * This version written by Charles Hawkins
 */
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>


const char up_arrow[4] = {0xE2, 0x86, 0x91, 0x00}; /* Unicode left-arrow glyph */
const char left_arrow[4] = {0xE2, 0x86, 0x90, 0x00};
const char *cmd_chars = "\"/=^<\n\rABCDEFGIJKLMPQRSTVW"; /* Characters typed by the user for each command */
char *cmd_strings[26] = {"\"", "/", "=", "↑", "←", "\r\n", "\r\n", "APPEND", "BUFFER #", "CHANGE", "DELETE", "EDIT", "FINISHED", "GET #", "INSERT", "JAM INTO #", "KILL #", "LOAD #", "MODIFY", "PRINT", "QUICK", "READ FROM ", "SUBSTITUTE ", "TABS", "VERBOSE", "WRITE ON "}; /* Sequences typed by qed for each command in VERBOSE mode */
char *cmd_strings_quick[26] = {"\"", "/", "=", "", "", "\r\n", "\r\n", "A", "B", "C", "D", "E", "F", "G", "I", "J", "K", "L", "M", "P", "Q", "R", "S", "T", "V", "W"}; /* Sequences typed by qed for each command in QUICK mode */
const int cmd_addrs[26] = {0, 2, 1, 0, 1, 2, 2, 1, 0, 2, 2, 2, 0, 2, 1, 0, 0, 2, 2, 2, 0, 1, 2, 0, 0, 2}; /* The number of addresses taken by each command (same order as above) */
const char *cmd_noconf = "\"/=^<\n\r"; /* Commands on this list are executed immediately, without the user typing a confirming . */ 
const char *cmd_noaddr = "\"BFJKQTV";
const int BUF_INCREMENT = 30; /* When a buffer runs out of space, we'll increase its size by this many characters */

/* Structure specifying the current state of the program, including the contents of the main and numbered buffers,
the current and last lines (dot and dollar), the file read from, and whether we're in quick mode. */
struct state_spec {
	char **main_buffer;
	char **aux_buffers;
	int *buf_sizes;
	int dot;
	int dollar;
	FILE *file;
	int quick;
	struct buffer_pos *buffer_stack;
};
/* Complete command specifier, including starting and ending lines, the command, 0-2 arguments, flags */
struct command_spec {
	struct line_spec *start;
	struct line_spec *end;
	char command;
	char *arg1;
	char *arg2;
	char flag;
	int num;
};

/* Structure containing a line specifier as entered on the command line. Can be absolute or relative, numerical or search-based. 
   Adding or subtracting addresses uses multiple line spec structs, chained into a linked list via the next pointer. */
struct line_spec {
	char sign;
	char type;
	int line;
	char *search;
	struct line_spec *next;
};

/* Because a buffer can itself contain buffer calls, including to the same buffer, we need a stack of buffers we're executing from, including the buffer and position within each buffer for each stack entry
 * This structure contains the stack of buffers currently being read from. Base pointer is to the current / innermost buffer, *prev points to the one outside of that (i.e. to be returned to when this one is done), and so on. If the base pointer is NULL it means we are just executing from stdin */
struct buffer_pos {
	int current_char;
	int buf_num;
	struct buffer_pos *prev;
};
void err(struct state_spec *state);
int buffer_for_char(char c);
void kill_buffer(int buffer_num, struct state_spec *state);
void set_buffer(int buffer_num, char *text, struct state_spec *state);
int find_string(char *search, int start_line, int is_tag, struct state_spec *state);
int substitute(char *replace, char *find, int start, int end, char mode, int num, struct state_spec *state);
char convert_esc(char c, struct state_spec *state);
int next_char(char *c, int convert, int echo, int ctl_v, struct state_spec *state);
void **replace_elements_in_vector(void **dest, int *dest_length, void **src, int src_length, int pos, int num);
void add_char_to_buffer(char **line, int *length, int *buf_size, char c, int unlimited, int echo);
char get_flags(struct command_spec *command, struct state_spec *state);
void get_buffer_name(struct command_spec *command, struct state_spec *state);
int get_string(char **str, int *length, char delim, int full, int unlimited, int literal, int oneline, char *oldline, struct state_spec *state);
char **get_lines(int *length, int literal, struct state_spec *state);
struct command_spec* get_command(struct state_spec *state);
int resolve_line_spec(struct line_spec *line, struct state_spec *state);
int execute_command(struct command_spec *command, struct state_spec *state);
int increase_buffer(char **buffer, size_t *size);
struct line_spec *new_line_spec(char sign, char type, int line, char *search);
void free_line_spec(struct line_spec *ls);
void free_command_spec(struct command_spec *cmd);
void free_buffer_stack(struct buffer_pos *stack);
char print_char(char c);
int print_buffer(char *buf);
int main(int argc, char **argv)
{
	struct command_spec *command;
	struct state_spec *state;
	struct line_spec *ls;
	int finished = 0;
	/* qed runs in terminal raw mode, so that characters typed by the user aren't echoed and so that we can do \r and \n separately when needed */
	struct termios qed_term_settings;
	struct termios original_term_settings;
	tcgetattr(fileno(stdin), &original_term_settings);
	qed_term_settings = original_term_settings;
	cfmakeraw(&qed_term_settings);
	tcsetattr(fileno(stdin), TCSANOW, &qed_term_settings);
	setbuf(stdout,NULL);

	state = malloc(sizeof(struct state_spec));
	state->main_buffer = calloc(sizeof(int*), 1);
	state->aux_buffers = calloc(sizeof(char*), 36);
	state->buf_sizes = calloc(sizeof(int*), 36);
	memset(state->aux_buffers, 0, sizeof(char*) * 36);
	memset(state->buf_sizes, 0, sizeof(int*) * 36);
	state->dollar = 0;
	state->dot = 0;
	state->file = NULL;
	state->quick = 0;
	state->buffer_stack = NULL;
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
			err(state);
			printf("\r\n");
		}
	} while(!finished);

	tcsetattr(fileno(stdin), TCSANOW, &original_term_settings);
	return 0;
}

void err(struct state_spec *state)
/* Called when there's any kind of error in a command. Prints out "?" and clears the stack of buffer execution. The manual suggests using the latter behavior as a form of flow control */
{
	printf("?\r\n");
	free_buffer_stack(state->buffer_stack);
	state->buffer_stack = NULL;
}
int buffer_for_char(char c)
/* Converts from a buffer name (one alphanumeric character) to the buffer number, indicating where in our array of buffers that buffer is stored */
{
	if(isdigit(c))
		return (int)c-'0';
	else if(isalpha(c))
		return c-'A'+10;
	else
		return -1;
}
void kill_buffer(int buffer_num, struct state_spec *state)
/* Executes the KILL buffer command, clearing the contents of the given-numbered buffer */
{
	if(state->aux_buffers[buffer_num])
		free(state->aux_buffers[buffer_num]);
	state->aux_buffers[buffer_num] = NULL;
	state->buf_sizes[buffer_num] = 0;
}
void set_buffer(int buffer_num, char *text, struct state_spec *state)
/* Sets the contents of the given-numbered buffer to the given text, such as by the JAM INTO command */
{
	kill_buffer(buffer_num, state);
	state->aux_buffers[buffer_num] = text;
	state->buf_sizes[buffer_num] = strlen(text);
}
int find_string(char *search, int start_line, int is_tag, struct state_spec *state)
/* Implements the behavior of searches [] and tag searches :: bu searching for the given string in the main buffer, starting from the given line and wrapping */
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
/* Implements the SUBSTITUTE command */
{
	int old_str_length, replace_length, find_length, new_str_length = 0, done = 0, line, num_subs = 0;
	replace_length = strlen(replace);
	find_length = strlen(find);
	for(line = start; line <= end; line++)
	{
		char *found;
		char *old_str = state->main_buffer[line];
		int made_sub = 0, start_from = 0;
		while((found = strstr(old_str+start_from, find)))
		{
			char *new_str;
			int pos = found-old_str;
			start_from = pos + replace_length;
			old_str_length = strlen(old_str);
			new_str_length = old_str_length - find_length + replace_length;
			if(num >= 0 &&num_subs >= num)
				return num_subs;
			new_str = malloc(new_str_length+1);
			new_str[0] = '\0';
			strncat(new_str, old_str, pos);
			if(mode == 'W' || mode == 'V')
			{
				char c, lastchar = '0';
				int skip = 0;
				printf("%s\"%s\"%s\r", new_str, find, found+find_length);
				do
				{
					next_char(&c, 1, 1, 0, state);
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
			strncat(new_str, replace, new_str_length);
			strncat(new_str, found+find_length, new_str_length);
			free(old_str);
			state->main_buffer[line] = old_str = new_str;
			num_subs++;
			made_sub = 1;
		}
		if(made_sub && (mode == 'L' || mode == 'V'))
		{
			print_buffer(state->main_buffer[line]);
		}
	}
	return num_subs;
}
char convert_esc(char c, struct state_spec *state)
/* Converts one or more characters for response and printing.  Capitalizes and turns relevant multi-character escape sequences into single command characters */
{
	if(c >= 'a' && c <= 'z') /* Make upper case if it's not already */
	{
		c += 'A'-'a';
	}
	else if(c == '\r')
	{
		c = '\n';
	}
	else if(c == 0x1B) /* We have an escape sequence */
	{
		next_char(&c, 0, 0, 0, state);
		if(c == 0x5B)
		{
			next_char(&c, 0, 0, 0, state);
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
/* Constructor for line specifiers */
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
void free_buffer_stack(struct buffer_pos *stack)
{
	if(stack)
	{
		free_buffer_stack(stack->prev);
		free(stack);
	}
}
int increase_buffer(char **buffer, size_t *size)
/* Allocates more space for the buffer. Why I didn't just use realloc() I'm not sure... */
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
char print_char(char c)
/* Prints the character c, converting it for printability as necessary (e.g. CR becomes CRLF, ^A becomes &A). Returns the char back so the caller can check for \0 */
{
	if(c == '\r' || c == '\n')
		printf("\r\n");
	else if(c && c <= (char)26 && c != '\t')	/* c is a control character */
	{
		putchar_unlocked((int)'&');
		putchar_unlocked((int)(c+'A'-1));
	}
	else
		putchar_unlocked((int)c);
	return c;
}
int print_buffer(char *buf)
/* Print a string using the print_char function */
{
	if(!buf)
		return 0;
	for(;*buf;buf++)
	{
		print_char(*buf);
	}
	return 1;
}
int next_char(char *c, int convert, int echo, int ctl_v, struct state_spec *state)
/* Read the next character from file, buffer, or stdin. Used when reading into a buffer of any kind, such as APPEND/INSERT/CHANGE, EDIT/MODIFY, JAM INTO, and searches/SUBSTITUTE */
{
	int status;
	if(state->file)
	{
		status = (char)getc_unlocked(state->file);
	}
	else if(state->buffer_stack)
	{
		while(1)
		{
			struct buffer_pos *current_pos = state->buffer_stack;
			current_pos->current_char++;
			if(current_pos->current_char < state->buf_sizes[current_pos->buf_num])	/* We're still inside the buffer, just grab the next char */
			{
				status = state->aux_buffers[current_pos->buf_num][current_pos->current_char];
				break;
			}
			else if(current_pos->current_char > state->buf_sizes[current_pos->buf_num])
				return 0;
			state->buffer_stack = current_pos->prev;
			free(current_pos);
			if(!(state->buffer_stack))
			{
				status = (char)getchar_unlocked();
				break;
			}
		}
	}
	else
	{
		status = (char)getchar_unlocked();
	}
	if(!status || status == EOF)
		return 0;
	if(status == 0x02 && !ctl_v)	/* CTL-B; execute buffer */
	{
		char b;
		printf("#");
		next_char(&b, 0, 1, 1, state);
		int buf_num = buffer_for_char(toupper(b));
		if(buf_num == -1)
		{
			err(state);
			*c = '\0';
			return 0;
		}
		if(state->buf_sizes[buf_num])
		{
			struct buffer_pos *new_pos = malloc(sizeof(struct buffer_pos));
			new_pos->current_char = -1;
			new_pos->buf_num = buf_num;
			new_pos->prev = state->buffer_stack;
			state->buffer_stack = new_pos;
			return next_char(c, convert, echo, 0, state);
		}
		else
			return next_char(c, convert, echo, 0, state);
	}
	*c = (char)status;
	if(convert)
		*c = convert_esc(*c, state);
	if(echo)
		putchar_unlocked((int)*c);
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
				print_char(c);
		}
	}
	else
	{
		(*line)[*length] = c;
		*length = *length+1;
		if(c != 0x04 && echo)
			print_char(c);
	}
}
char get_flags(struct command_spec *command, struct state_spec *state)
/* Called when we're expecting flags for the SUBSTITUTE command; reads them in and sets the flag member variable in the command spec */
{
	char c;
	do{
		next_char(&c, 1, 1, 0, state);
	} while(c == ' ' || c == '\t');
	while(c == ':')
	{
		int n = 0;
		int digit = 0;
		next_char(&c, 1, 0, 0, state);
		while(c >= '0' && c <= '9')
		{
			digit = 1;
			print_char(c);
			n = n * 10 + c - '0';
			command->num = n;
			next_char(&c, 1, 0, 0, state);
		}
		if(!digit)
		{
			if(c == 'G' || c == 'W' || c == 'L' || c == 'V')
			{
				printf("%c",c);
				command->flag = c;
				do{
					next_char(&c, 1, 1, 0, state);
				} while(c == ' ' || c == '\t');
			}
			else
			{
				return '\0';
			}
		}
		else
		{
			print_char(c);
		}
	}
	return c;
}
void get_buffer_name(struct command_spec *command, struct state_spec *state)
/* Called when we're expecting a buffer name, such as with JAM INTO. Reads it in and sets the arg1 of command to a string containing that character */
{
	char c;
	next_char(&c, 1, 0, 0, state);
	if((c>= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))
	{
		printf("%c",c);
		command->arg1 = malloc(2);
		command->arg1[0] = c;
		command->arg1[1] = '\0';
	}
}
int get_string(char **str, int *length, char delim, int full, int unlimited, int literal, int oneline, char *oldline, struct state_spec *state)
/* Gets a line of text from the user / buffer / file. Used by INSERT, APPEND, CHANGE, EDIT, and MODIFY, along with READ FROM. Handles all the special control characters for EDIT/MODIFY, and the smaller array of control characters for the others */
{
	char c;
	int newstr = 1;
	int stop = 0;
	int buf_size = BUF_INCREMENT;
	int my_length;
	int status;
	int oldpos = 0;
	int oldlen = 0;
	int insert = 0;
	if(oldline) {oldlen = strlen(oldline);}
	if(!length) {length = &my_length;}
	*str = malloc(buf_size+1);
	*length = 0;
	do
	{
		status = next_char(&c, 0, 0, 0, state);
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
					status = next_char(&c, 0, 0, 1, state);
					add_char_to_buffer(str, length, &buf_size, c, unlimited, !literal);
					break;
				default:
					if(oldline)
					{
						switch(c)
						{
							int found;
							char findchar;
							case 0x03:	/* Ctrl-C (next char) */
								if(oldpos < oldlen-1)
									add_char_to_buffer(str, length, &buf_size, oldline[oldpos], unlimited, 1);
								else
									putchar_unlocked(7);	/* Ring bell */
								oldpos++;
								break;
							case 0x08:	/* Ctrl-H (copy rest of line) */
							case 0x04:	/* Ctrl-D (copy rest of line and terminate) */
							case 0x06:	/* Ctrl-F (copy rest of line, no typing, and terminate) */
								while(oldpos<oldlen-1)
								{
									add_char_to_buffer(str, length, &buf_size, oldline[oldpos], unlimited, (c != 0x06));
									oldpos++;
								}
								if (c == 0x08)
									break;
							case '\r':
								stop = 1;
								add_char_to_buffer(str, length, &buf_size, '\n', unlimited, 1);
								break;
							case 0x0F: 	/* Ctrl-O (copy until character) */
							case 0x1A:	/* Ctrl-Z (copy through character) */
								next_char(&findchar, 0, 0, 0, state);
								found = oldpos+(c == 0x0F);	/* start at oldpos for ctrl-z, oldpos+1 for ctrl-o */
								while (found < oldlen)
								{
									if(oldline[found] == findchar)
										break;
									found++;
								}
								if(found >= oldlen-1)
									putchar_unlocked(7);
								else
								{
									if(c == 0x0F)
										found--;
									while(oldpos <= found)
									{
										add_char_to_buffer(str, length, &buf_size, oldline[oldpos], unlimited, 1);
										oldpos++;
									}
								}
								break;
							case 0x13:	/* Ctrl-S (skip character) */
								if(oldpos<oldlen-1)
								{
									oldpos++;
									print_char('%');
								}
								else
								{
									putchar_unlocked(7);
								}
								break;
							case 0x05:	/* Ctrl-E (toggle insert mode) */
								print_char(insert?'>':'<');
								insert = !insert;
								break;
							case 0x0E:	/* Ctrl-N (delete character, restorative) */
								if(*length)
								{
									*length = *length-1;
								}
								printf("%s", up_arrow);
								if(!(*length))
								{
									printf("\r\n");
								}
								if(oldpos > 0)
									oldpos--;
								break;
							case 0x12:	/* Ctrl-R (type rest of old line, then new line, old aligned with old) */
								putchar_unlocked((int)'\n');
								print_buffer(oldline+oldpos);
								//print_char('\n');
								print_buffer(*str);
								break;
								break;
							default:
								add_char_to_buffer(str, length, &buf_size, c, unlimited, 1);
								if (oldpos < oldlen-1 && !insert)
									oldpos++;
						}
					}
					else if(c == delim)
					{
						if(c > 26)
							print_char(c);
						stop = 1;
					}
					else if(full)
					{
						if(c == 0x04)
						{
							//printf("\r\n");
							stop = 1;
							//break;
						}
						else if(c == '\r')
						{
							if(oneline)
							{
								stop = 1;
							}
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
/* Gets multiple lines of text for APPEND/INSERT/CHANGE/EDIT/MODIFY/READ FROM by calling get_string() repeatedly */
{
	char **input_lines = NULL;
	char *buffer = NULL;
	char **one_line = NULL;
	int buffer_length = 0;
	int done = 0;
	*length = 0;
	do {
		get_string(&buffer, &buffer_length, '\0', 1, 1, literal, 1, NULL, state);
		if(buffer[buffer_length-2] == 0x04)
			done = 1;
		if(buffer[0] != 0x04)
		{
			buffer[buffer_length-2] = '\n';
			one_line = malloc(sizeof(void*));
			*one_line = buffer;
			if(done)
				printf("\r\n");
			input_lines = (char**)replace_elements_in_vector((void**)(input_lines), length, (void**)one_line, 1, *length, 0);
		}
		else
		{
			free(buffer);
		}
	} while(!done);
	return input_lines;
}
struct command_spec* get_command(struct state_spec *state)
/* Reads a command from stdin/a buffer and decodes it into a command_spec struct. Returns NULL if there is an error while reading the command */
{
	char c = '\0';
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
		if(!next_char(&c, 0, 0, 0, state))
		/* Input stream has closed! Complain and exit. */
		{
			err(state);
			exit(1);
		}
		c = convert_esc(c, state);
		if(c == 0x7F) /* Received a backspace ("rubout"). Pressing it twice cancels the command */
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
			print_char(c);
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
			putchar_unlocked((int)c);
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
			putchar_unlocked((int)c);
		}
		else if(c == ':' || c == '[')
		{
			putchar_unlocked((int)c);
			if(*line == NULL)
			{
				*line = new_line_spec('+',c,0,NULL);
				get_string(&((*line)->search),&((*line)->line), c=='['?']':c, 0, c=='[', 0, 1, NULL, state);
			}
			else if((*line)->type == 'c')
			{
				(*line)->type = c;
				get_string(&((*line)->search),&((*line)->line), c=='['?']':c, 0, c=='[', 0, 1, NULL, state);
			}
			else
			{
				line = &((*line)->next);
				*line = new_line_spec('+',c,0,NULL);
				get_string(&((*line)->search),&((*line)->line), c=='['?']':c, 0, c=='[', 0, 1, NULL, state);
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
			putchar_unlocked((int)c);
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
					get_buffer_name(command, state);
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
						next_char(&c, 0, 1, 0, state);
					} while(c == ' ' || c == '\t' || c == '\n');
					get_string(&(command->arg1), NULL, c, 0, 1, 0, 1, NULL, state);
				}
				else if(c == 'S')
				{
					c = get_flags(command, state);
					if(!c)
					{
						free_command_spec(command);
						return NULL;
					}
					get_string(&(command->arg1), NULL, c, 0, 1, 0, 1, NULL, state);
					printf(" FOR ");
					print_char(c);
					get_string(&(command->arg2), NULL, c, 0, 1, 0, 1, NULL, state);
					if(strlen(command->arg2) == 0)
					{
						free_command_spec(command);
						return NULL;
					}
				}
				next_char(&c, 1, 0, 0, state);
				if(c != '.')
				{
					free_command_spec(command);
					return NULL;
				}
				print_char(c);
			}
		}
		else {printf("%c", 0x07);}
	} while(!done);
	return command;
}
int resolve_line_spec(struct line_spec *line, struct state_spec *state)
/* Given a line_spec struct, which may involve searches, relative offsets, etc., resolves it to an actual line number */
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
			char *buf;
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
			buf = malloc(strlen(line->search));
			strcpy(buf, line->search);
			set_buffer(0, buf, state);
			if(!(line_number = find_string(line->search, first?state->dot+1:line_number, 1, state))) {return -1;}
			break;
			case '[':
			buf = malloc(strlen(line->search));
			strcpy(buf, line->search);
			set_buffer(0, buf, state);
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
/* Takes a command_spec struct, generated by get_command(), and executes it on the current state */
{
	int line1 = state->dot, line2 = state->dot;
	char *sep = "\r";
	if(command->start)
	{
		line1 = resolve_line_spec(command->start, state);
		if(line1 == -1)
		{
			err(state);
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
			err(state);
			return 0;
		}
	}
	if(line2 == 0 && command->command != 'A' && command->command != '=' && command->command != 'R')
		line2 = 1;
 	if(command->command == '\n' && !command->start) 
		line2 = ++line1;
	if(!strchr(cmd_noaddr, command->command) && (line2 < line1 || line2 > state->dollar))
	{
		err(state);
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
			err(state);
			return 0;
		}
		state->dot = state->dot - 1;
		print_buffer(state->main_buffer[state->dot]);
		break;
	case '=':
		printf("%i\r\n", line1);
		break;
	case 'P':
		printf("\r\nDOUBLE? ");
		next_char(&c, 1, 1, 0, state);
		printf("\r\n");
		if(c == 'Y')
			sep = "\r\n";
		else if(c == 'N')
			sep = "";
		else
		{
			err(state);
			return 0;
		}
	/* Intentional fallthrough */
	case '/':
	case '\n':
		for(i=line1; i<=line2; i++)
		{
			print_buffer(state->main_buffer[i]);
			printf("%s", sep);
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
			get_string(&buffer, &length, '\0', 1, 1, 0, 1, NULL, state);
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
				if(done)
					printf("\r\n");
			}
			else
			{
				free(buffer);
			}
		} while(!done);
		break;
	case 'C':
		input_lines = get_lines(&input_length, 0, state);
		length = state->dollar+1;
		state->main_buffer = (char**)replace_elements_in_vector((void**)(state->main_buffer), &(length), (void**)input_lines, input_length, line1, line2-line1+1);
		state->dollar = length-1;
		state->dot = line1 + input_length - 1;
		break;
	case 'E':
	case 'M':
		for(int line=line1; line<=line2; line++)
		{
			if(command->command == 'E')
				print_buffer(state->main_buffer[line]);
			get_string(&buffer, &buffer_length, '\0', 1, 1, 0, 1, state->main_buffer[line], state);
			input_lines = malloc(sizeof(char*));
			input_lines[0] = buffer;
			length = state->dollar+1;
			state->main_buffer = (char**)replace_elements_in_vector((void**)(state->main_buffer), &(length), (void**)input_lines, 1, line, 1);
			state->dollar = length-1;
			state->dot = line;
		}
		break;
	case 'L':
	case 'G':
		length = line2-line1+1;
		buffer_length = 1; //length for the newlines and 1 for the terminating \0
		for (i=line1; i<=line2; i++)
		{
			buffer_length += strlen(state->main_buffer[i]);
		}
		buffer = malloc(buffer_length);
		buffer[0] = '\0';
		for(int i=line1; i<=line2; i++)
		{
			strcat(buffer, state->main_buffer[i]);
			buffer[strlen(buffer)-1] = '\r';
		}
		set_buffer(buffer_for_char(command->arg1[0]), buffer, state);
		state->buf_sizes[buffer_for_char(command->arg1[0])] = strlen(buffer);
		if (command->command == 'L')
			break;
	case 'D':
		length = state->dollar+1;
		state->main_buffer = (char**)replace_elements_in_vector((void**)(state->main_buffer), &(length), NULL, 0, line1, line2-line1+1);
		state->dollar = length-1;
		state->dot = line1-1;
		break;
	case 'R':
		if(!(state->file = fopen(command->arg1, "r")))
		{
			err(state);
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
			err(state);
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
			err(state);
		else
			printf("%i\r\n", n);
		break;
	case 'J':
		get_string(&buffer, &buffer_length, '\0', 1, 1, 0, 0, NULL, state);
		buffer[buffer_length-2] = '\0';
		if(buffer_length > 2 && buffer[buffer_length-3] != '\r')
			printf("\r\n");
		set_buffer(buffer_for_char(command->arg1[0]), buffer, state);
		state->buf_sizes[buffer_for_char(command->arg1[0])] = strlen(buffer);
		break;
	case 'K':
		kill_buffer(buffer_for_char(command->arg1[0]), state);
		break;
	case 'B':
		if(state->aux_buffers[buffer_for_char(command->arg1[0])])
		{
			printf("\"");
			print_buffer(state->aux_buffers[buffer_for_char(command->arg1[0])]);
			printf("\"\r\n");
		}
		break;
	case 'F':
			return 1;
	default:
		printf("[not implemented yet]\r\n");
	}
	return 0;
}
