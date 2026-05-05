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


const char *dumpfile = "/tmp/qed-dump";
const int dumprev = 1;
const char up_arrow[4] = {0xE2, 0x86, 0x91, 0x00}; /* Unicode left-arrow glyph */
const char left_arrow[4] = {0xE2, 0x86, 0x90, 0x00};
const char *cmd_chars = "\"/=^<\n\rABCDEFGIJKLMPQRSTVW"; /* Characters typed by the user for each command */
char *cmd_strings_verbose[26] = {"\"", "/", "=", "↑", "←", "\r\n", "\r\n", "APPEND", "BUFFER #", "CHANGE", "DELETE", "EDIT", "FINISHED", "GET #", "INSERT", "JAM INTO #", "KILL #", "LOAD #", "MODIFY", "PRINT", "QUICK", "READ FROM ", "SUBSTITUTE ", "TABS", "VERBOSE", "WRITE ON "}; /* Sequences typed by qed for each command in VERBOSE mode */
char *cmd_strings_quick[26] = {"\"", "/", "=", "", "", "\r\n", "\r\n", "A", "B", "C", "D", "E", "F", "G", "I", "J", "K", "L", "M", "P", "Q", "R", "S", "T", "V", "W"}; /* Sequences typed by qed for each command in QUICK mode */
char **cmd_strings = cmd_strings_verbose;
const int cmd_addrs[26] = {0, 2, 1, 0, 1, 2, 2, 1, 0, 2, 2, 2, 0, 2, 1, 0, 0, 2, 2, 2, 0, 1, 2, 0, 0, 2}; /* The number of addresses taken by each command (same order as above) */
const char *cmd_noconf = "\"/=^<\n\r"; /* Commands on this list are executed immediately, without the user typing a confirming . */ 
const char *cmd_noaddr = "\"BFJKQTV";
const int BUF_INCREMENT = 30; /* When a buffer runs out of space, we'll increase its size by this many characters */
const int NUM_AUX_BUFS = 36; /* Number of aux buffers. They are named 0-9 and A-Z, so 36 in total */

/* Flags for use in various functions */
const int FL_NONE = 0;
const int FL_ECHO = 1;
const int FL_FULL = 2;
const int FL_ONELINE = 4;
const int FL_LITERAL = 8;
const int FL_REALLOC = 16;
const int FL_SKIP = 32;
const int FL_CONVERT = 64;
#define set_flags(var,flags) var |= (flags);
#define unset_flags(var,flags) var &= ^(flags);
/* Structure keeping track of a string buffer */
struct string {
	int length;
	size_t space;
	char *buf;
};
/* Structure specifying the current state of the program, including the contents of the main and numbered buffers,
the current and last lines (dot and dollar), the file read from, and whether we're in quick mode. */
struct state_spec {
	struct string *main_buffer;
	struct string *aux_buffers;
	int dot;
	int dollar;
	FILE *file;
	int quick;
	int wrote_out;
	struct buffer_pos *buffer_stack;
};
/* Complete command specifier, including starting and ending lines, the command, 0-2 arguments, flags */
struct command_spec {
	struct line_spec *start;
	struct line_spec *end;
	char command;
	struct string arg1;
	struct string arg2;
	char flag;
	int num;
};

/* Structure containing a line specifier as entered on the command line. Can be absolute or relative, numerical or search-based. 
   Adding or subtracting addresses uses multiple line spec structs, chained into a linked list via the next pointer. */
struct line_spec {
	char sign;
	char type;
	int line;
	struct string search;
	struct line_spec *next;
};

/* Because a buffer can itself contain buffer calls, including to the same buffer, we need a stack of buffers we're executing from, including the buffer and position within each buffer for each stack entry
 * This structure contains the stack of buffers currently being read from. Base pointer is to the current / innermost buffer, *prev points to the one outside of that (i.e. to be returned to when this one is done), and so on. If the base pointer is NULL it means we are just executing from stdin */
struct buffer_pos {
	int current_char;
	int buf_num;
	struct buffer_pos *prev;
};
void dbg_string(struct string *s)
{
	if(!s)
	{
		printf("<NULL>");
		return;
	}
	printf("<s:%zu l:%i b:[", s->space, s->length);
	if(!s->buf)
		printf("NULL");
	else
	{
		for(int i=0; i<=s->length; i++)
			printf(" %02x",s->buf[i]);
		if (s->length > 0 && s->buf[s->length] == '\0')
		{
			printf(" | \"");
			for(int i=0; i<s->length; i++)
			{
				char c = s->buf[i];
				if(c <= 0x1a)
					printf("^%c", c+('A'-0x01));
				else
					printf("%c", c);
			}
			printf("\"");
		}
		printf(" ]>");
	}
}
void dbg_lines(struct string *lines, int n_lines, int start_at)
{
	for(int i = start_at; i<n_lines; i++)
	{
		printf("%i: ", i);
		dbg_string(&lines[i]);
		printf("\r\n");
	}
}
void ring_bell() {putchar_unlocked(7);}
void err(struct state_spec *state);
struct string *new_string();
struct string *empty_string(struct string *s);
struct string *string_with_capacity(struct string *s, size_t space);
struct string *string_from_cstring(struct string *s, char *cs);
struct string *capture_cstring(struct string *s, char *cs, size_t space);
void delete_string(struct string *s);
void free_string(struct string *s);
struct string *copy_string(struct string *dst, struct string *src, int copy_space);
struct string *cat_slice(struct string *dst, struct string *src, int start, int length);
void cat_strings(struct string *s1, struct string *s2);
int print_string(struct string *s);
struct string *read_string_from_file(struct string *s, int length, FILE *f);
int string_starts_with_char(struct string *s, char c);
int string_ends_with_char(struct string *s, char c);
void string_backspace(struct string *s);
int buffer_for_char(char c);
void kill_buffer(int buffer_num, struct state_spec *state);
void set_buffer(int buffer_num, struct string *new_text, struct state_spec *state);
int find_string(struct string *search, int start_line, int is_tag, struct state_spec *state);
int substitute(struct string *replace, struct string *find, int start, int end, char mode, int num, struct state_spec *state);
char convert_esc(char c, struct state_spec *state);
int next_char(char *c, int convert, int echo, int ctl_v, struct state_spec *state);
void **replace_elements_in_vector(void **dest, int *dest_length, void **src, int src_length, int pos, int num);
void add_char_to_string(struct string *str, char c, int realloc, int echo, int skip, struct string *lbuf);
char get_flags(struct command_spec *command, struct state_spec *state);
void get_buffer_name(struct command_spec *command, struct state_spec *state);
int get_string(struct string *str, char delim, int full, int unlimited, int literal, int oneline, struct string *oldline, struct state_spec *state);
int main_buffer_text_entry(int insert_point, int num_lines_replaced, int edit_mode, int type_out, struct state_spec *state);
struct command_spec* get_command(struct state_spec *state);
int resolve_line_spec(struct line_spec *line, struct state_spec *state);
int execute_command(struct command_spec *command, struct state_spec *state);
struct line_spec *new_line_spec(char sign, char type, int line, struct string *search);
void free_line_spec(struct line_spec *ls);
void free_command_spec(struct command_spec *cmd);
void free_buffer_stack(struct buffer_pos *stack);
void free_state_spec(struct state_spec *state);
char print_char(char c);
int print_buffer(char *buf);
void dump_state(struct state_spec *state);
struct state_spec* restore_state();

int main(int argc, char **argv)
{
	struct command_spec *command;
	struct state_spec *state;
	struct line_spec *ls;
	int finished = 0;
	int cont_flag = 0;
	for (int i=1; i<argc; i++)
	{
		if (!strcmp(argv[i], "-c"))
		{
			cont_flag = 1;
		}
	}
	/* qed runs in terminal raw mode, so that characters typed by the user aren't echoed and so that we can do \r and \n separately when needed */
	struct termios qed_term_settings;
	struct termios original_term_settings;
	tcgetattr(fileno(stdin), &original_term_settings);
	qed_term_settings = original_term_settings;
	cfmakeraw(&qed_term_settings);
	tcsetattr(fileno(stdin), TCSANOW, &qed_term_settings);
	setbuf(stdout,NULL);

	if(cont_flag)
	{
		state = restore_state();
		if (!state)
		{
			return 1;
		}
	}
	else
	{
		state = malloc(sizeof(struct state_spec));
		state->main_buffer = calloc(sizeof(struct string), 1);
		state->aux_buffers = calloc(sizeof(struct string), NUM_AUX_BUFS);
		memset(state->aux_buffers, 0, sizeof(struct string) * NUM_AUX_BUFS);
		state->dollar = 0;
		state->dot = 0;
		state->file = NULL;
		state->quick = 0;
		state->wrote_out = 1;
		state->buffer_stack = NULL;
	}
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
	if (!state->wrote_out) {
		printf("WRITE OUT!\r\n");
	}

	dump_state(state);
	free_state_spec(state);
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
	delete_string(&state->aux_buffers[buffer_num]);
}
void set_buffer(int buffer_num, struct string *new_text, struct state_spec *state)
/* Sets the contents of the given-numbered buffer to the given text, such as by the JAM INTO command */
{
	copy_string(&state->aux_buffers[buffer_num], new_text, 0);
}
int find_string(struct string *search, int start_line, int is_tag, struct state_spec *state)
/* Implements the behavior of searches [] and tag searches :: by searching for the given string in the main buffer, starting from the given line and wrapping */
{
	int i;
	char *found;
	char next;
	for(i = start_line; i <= state->dollar; i++)
	{
		found = strstr(state->main_buffer[i].buf, search->buf);
		if(is_tag)
		{
			next = found?found[search->length]:'0';
			if(found == state->main_buffer[i].buf && next && !isalnum(next))
				return i;
		}
		else if(found)
			return i;
	}
	for(i = 1; i < start_line; i++)
	{
		found = strstr(state->main_buffer[i].buf, search->buf);
		if(is_tag)
		{
			next = found?found[search->length]:'0';
			if(found == state->main_buffer[i].buf && next && !isalnum(next))
				return i;
		}
		else if(found)
			return i;
	}
	return 0;
}
int substitute(struct string *replace, struct string *find, int start, int end, char mode, int num, struct state_spec *state)
/* Implements the SUBSTITUTE command */
{
	int num_subs = 0;
	for(int line = start; line <= end; line++)
	{
		char *found;
		struct string *old_str = &state->main_buffer[line];
		int made_sub = 0, start_from = 0;
		while((found = strstr(old_str->buf+start_from, find->buf)))
		{
			if(num >= 0 &&num_subs >= num)
				return num_subs;
			int pos = found-old_str->buf;
			start_from = pos + replace->length;
			struct string *new_str = cat_slice(NULL, old_str, 0, pos);
			//dbg_string(new_str);
			if(mode == 'W' || mode == 'V')  /* "ask-the-user" mode */
			{
				char c, lastchar = '0';
				int skip = 0;
				printf("%s\"%s\"%s\r", new_str->buf, find->buf, found+find->length);
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
					free_string(new_str);
					continue;
				}
			}
			cat_strings(new_str, replace);
			//dbg_string(new_str);
			cat_slice(new_str, old_str, pos + find->length, -1);
			//dbg_string(new_str);
			state->main_buffer[line] = *new_str;
			free(new_str);
			num_subs++;
			made_sub = 1;
		}
		if(made_sub && (mode == 'L' || mode == 'V'))
		{
			print_string(&state->main_buffer[line]);
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
	if(ls->search.buf)
	{
		delete_string(&ls->search);
	}
	free(ls);
}
struct line_spec *new_line_spec(char sign, char type, int line, struct string *search)
/* Constructor for line specifiers */
{
	struct line_spec *ls = malloc(sizeof(struct line_spec));
	ls->sign = sign;
	ls->type = type;
	ls->line=line;
	empty_string(&ls->search);
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
	if(cmd->arg1.buf)
	{
		delete_string(&cmd->arg1);
	}
	if(cmd->arg2.buf)
	{
		delete_string(&cmd->arg2);
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
void free_state_spec(struct state_spec *state)
{
	for(int i = 0; i <= state->dollar; i++)
	{
		delete_string(&state->main_buffer[i]);
	}
	free(state->main_buffer);
	for(int i = 0; i < NUM_AUX_BUFS; i++)
	{
		delete_string(&state->aux_buffers[i]);
	}
	free(state->aux_buffers);
	if (state->file)
		free(state->file);
	free_buffer_stack(state->buffer_stack);
	free(state);
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
		//printf("(bufferstack %i)", state->buffer_stack->buf_num); //DEBUG
		while(1)
		{
			struct buffer_pos *current_pos = state->buffer_stack;
			current_pos->current_char++;
			if(current_pos->current_char < state->aux_buffers[current_pos->buf_num].length)	/* We're still inside the buffer, just grab the next char */
			{
				status = state->aux_buffers[current_pos->buf_num].buf[current_pos->current_char];
				//printf("[0x%x]", status); //DEBUG
				break;
			}
			else if(current_pos->current_char > state->aux_buffers[current_pos->buf_num].length)
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
	if(status == 0x02 && !ctl_v && !state->file)	/* CTL-B; execute buffer */
	{
		char b;
		printf("#");
		next_char(&b, 0, 1, 1, state);
		if(b == '\\')
		{
			printf("\r\n");
			dbg_lines(state->main_buffer, state->dollar+1, 1);
			*c = '\0';
			return next_char(c, convert, echo, 0, state);
		}
		int buf_num = buffer_for_char(toupper(b));
		if(buf_num == -1)
		{
			printf("?");
			*c = '\0';
			return next_char(c, convert, echo, 0, state);
		}
		if(state->aux_buffers[buf_num].length)
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
struct string *replace_elements_in_string_vector(struct string *dest, int *dest_length, struct string *src, int src_length, int pos, int num)
/* Inserts all of the strings from src into dest at position pos, replacing num of dest's existing strings. All replaced strings are freed. Dest keeps the strings from src and src itself should be freed by the caller after. dest_length should point to dest's length, and this will be set to the length of the new dest. The new dest is returned. */
{
	int i, new_length;
	new_length = *dest_length - num + src_length;
	if(num < src_length)  /* We are adding more than we are replacing */
	{
		dest = realloc(dest, new_length * sizeof(struct string));
		for(i = *dest_length-1; i >= pos+num; i--)
		{
			dest[i+new_length-*dest_length] = dest[i];
		}
	}
	else if(num > src_length)  /* We are adding fewer elements than we are replacing */
	{
		for(i = pos+src_length; i < new_length; i++)
		{
			if(i < pos+num)
				delete_string(&dest[i]);
			dest[i] = dest[i+*dest_length-new_length];
		}
		dest = realloc(dest, new_length * sizeof(struct string));
	}
	for(i = 0; i < src_length; i++)
	{
		if(i < num)
			delete_string(&dest[i+pos]);
		dest[i+pos] = src[i];
	}
	*dest_length = new_length;
	return dest;
}
void add_char_to_string(struct string *str, char c, int reallocate, int echo, int skip, struct string *lbuf)
/* Adds the character c to the string str. If unlimited is true, the buffer will be reallocated if needed to make room for the new character; otherwise characters past the end are silently dropped. */
{
	if(lbuf)
		add_char_to_string(lbuf, c, 1, 0, 0, NULL);
	if(skip)
	{
		if(c != 0x04 && echo)
			print_char(c);
		return;
	}
	if(str->length >= str->space)
	{
		if(reallocate)
		{
			str->space += BUF_INCREMENT;
			str->buf = realloc(str->buf, str->space+1);
			str->buf[str->length] = c;
			str->length ++;
			if(c != 0x04 && echo)
				print_char(c);
		}
	}
	else
	{
		str->buf[str->length] = c;
		str->length ++;
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
		string_from_cstring(&command->arg1, " ");
		command->arg1.buf[0] = c;
	}
}
void finish_l_buffer(struct string **ctrl_l_buffer, struct state_spec *state)
{
	if ((*ctrl_l_buffer)->buf[(*ctrl_l_buffer)->length-1] == 0x04)
		(*ctrl_l_buffer)->length--;
	(*ctrl_l_buffer)->buf[(*ctrl_l_buffer)->length] = 0;
	set_buffer(1, (*ctrl_l_buffer), state);
	free((*ctrl_l_buffer));
	(*ctrl_l_buffer) = NULL;
}
int get_string(struct string *str, char delim, int full, int unlimited, int literal, int oneline, struct string *oldline, struct state_spec *state)
/* Gets a line of text from the user / buffer / file. Used by INSERT, APPEND, CHANGE, EDIT, and MODIFY, along with READ FROM. Handles all the special control characters for EDIT/MODIFY, and the smaller array of control characters for the others */
{
	char c;  /* Holds the most recent character read from the input source (user, buffer, file) */
	int stop = 0;  /* Set to 1 if we're done with this line and should return it */
	int status;  /* Status of the last call to next_char, indicating whether a character was successfully gotten. Will be 0 if we've reached the end of the file or buffer we're taking input from */
	int oldpos = 0;  /* Cursor position in the old line */
	int insert = 0;  /* Whether insert mode is on, causing typed characters to be inserted in EDIT/MODIFY rather than overwriting the old line */
	int skip_mode = 0;  /* Ctrl-K mode where no chars are added */
	int del_line = 0; /* Will be returned; indicates that Ctrl-Q was pressed on an empty line and therefore another line above this one should be deleted */
	struct string *ctrl_l_buffer = NULL;  /* Special buffer for the Ctrl-L command */
	struct string *refline = oldline?copy_string(NULL, oldline, 0):new_string();
	if(!str || !str->buf)
		empty_string(str);
	do
	{
		//dbg_string(str);
		status = next_char(&c, 0, 0, 0, state);
		if(!status)
		{
			stop = 1;
			add_char_to_string(str, 0x04, unlimited, !literal, skip_mode, ctrl_l_buffer);
		}
		else if(literal)
		{
			add_char_to_string(str, c, unlimited, !literal, skip_mode, ctrl_l_buffer);
			stop = (c == '\n');
		}
		else
		{
			switch(c)
			{
				case 0x01:	/* Ctrl-A (Delete Character) */
					if(str->length)
					{
						str->length --;
					}
					printf("%s", up_arrow);
					if(!(str->length))
					{
						printf("\r\n");
					}
					break;
				case 0x17:	/* Ctrl-W (Delete Word) */
					printf("\\");
					/* Delete any spaces at the end of the string */
					while((str->length)&&((str->buf)[str->length-1] == ' '||(str->buf)[str->length-1] == '\t'))
					{
						str->length--;
					}
					/* Delete non-blank chars from the end */
					while((str->length)&&((str->buf)[str->length-1] != ' '&&(str->buf)[str->length-1] != '\t'))
					{
						str->length--;
					}
					if(!(str->length))
					{
						printf("\r\n");
					}
					break;
				case 0x11:	/* Ctrl-Q (Delete Line) */
					printf("%s\r\n",left_arrow);
					if(str->length == 0)
					{
						del_line = 1;
						stop = 1;
					}
					str->length = 0;
					oldpos = 0;
					break;
				case 0x16:	/* Ctrl-V (Literal Character) */
					status = next_char(&c, 0, 0, 1, state);
					add_char_to_string(str, c, unlimited, !literal, skip_mode, ctrl_l_buffer);
					break;
				case 0x0c:     /* Ctrl-L (special buffer) */
					if (ctrl_l_buffer)
					{
						printf("]");
						finish_l_buffer(&ctrl_l_buffer, state);
					}
					else
					{
						printf("[");
						kill_buffer(1, state);
						ctrl_l_buffer = empty_string(NULL);
					}
					break;
				case 0x0b:  /* Ctrl-K mode; no chars added */
					printf("\"");
					skip_mode = !skip_mode;
					break;
				default:
					if(refline->buf)
					{
						switch(c)
						{
							int found;
							char findchar;
							case 0x03:	/* Ctrl-C (next char) */
								if(oldpos < refline->length-1)
									add_char_to_string(str, refline->buf[oldpos], unlimited, 1, skip_mode, ctrl_l_buffer);
								else
									ring_bell();
								oldpos++;
								break;
							case 0x08:	/* Ctrl-H (copy rest of line) */
							case 0x19:  /* Ctrl-Y (copy rest of line and re-edit */
							case 0x04:	/* Ctrl-D (copy rest of line and terminate) */
							case 0x06:	/* Ctrl-F (copy rest of line, no typing, and terminate) */
								while(oldpos<refline->length-1)
								{
									add_char_to_string(str, refline->buf[oldpos], unlimited, (c != 0x06 && c != 0x19), skip_mode, ctrl_l_buffer);
									oldpos++;
								}
								if (c == 0x19)
								{
									add_char_to_string(str, '\n', unlimited, 1, 0, ctrl_l_buffer);
									str->buf[str->length] = '\0';
									copy_string(refline, str, 0);
									empty_string(str);
									oldpos = 0;
								}
								if (c == 0x08 || c == 0x19)
									break;
							case '\r':
								stop = 1;
								add_char_to_string(str, '\n', unlimited, 1, skip_mode, ctrl_l_buffer);
								break;
							case 0x0F: 	/* Ctrl-O (copy until character) */
							case 0x1A:	/* Ctrl-Z (copy through character) */
							case 0x10:  /* Ctrl-P (Skip until character) */
							case 0x18:  /* Ctrl-X (Skip through character) */
								next_char(&findchar, 0, 0, 0, state);
								found = oldpos+(c == 0x0F || c == 0x10);	/* start at oldpos for ctrl-z, oldpos+1 for ctrl-o */
								while (found < refline->length)
								{
									if(refline->buf[found] == findchar)
										break;
									found++;
								}
								if(found >= refline->length-1)
									ring_bell();
								else
								{
									if(c == 0x0F || c == 0x10)
										found--;
									while(oldpos <= found)
									{
										if(c == 0x0F || c == 0x1A)
											add_char_to_string(str, refline->buf[oldpos], unlimited, 1, skip_mode, ctrl_l_buffer);
										else
											print_char('%');
										oldpos++;
									}
								}
								break;
							case 0x13:	/* Ctrl-S (skip character) */
								if(oldpos<refline->length-1)
								{
									oldpos++;
									print_char('%');
								}
								else
								{
									ring_bell();
								}
								break;
							case 0x05:	/* Ctrl-E (toggle insert mode) */
								print_char(insert?'>':'<');
								insert = !insert;
								break;
							case 0x0E:	/* Ctrl-N (delete character, restorative) */
								if(str->length)
								{
									str->length--;
								}
								printf("%s", up_arrow);
								if(!(str->length))
								{
									printf("\r\n");
								}
								if(oldpos > 0)
									oldpos--;
								break;
							case 0x14:      /* Ctrl-T (type rest of old line, then new line, old aligned with new */
							case 0x12:	/* Ctrl-R (type rest of old line, then new line, old aligned with old) */
								putchar_unlocked((int)'\n');
								if (c == 0x14) {
									putchar_unlocked((int)'\r');
									for (int i = 0; i < str->length; i++) {putchar_unlocked((int)' ');}
								}
								print_buffer(refline->buf+oldpos);
								//print_char('\n');
								//print_buffer(*str);
								for (int i = 0; i < str->length; i++) {putchar_unlocked((str->buf)[i]);}
								break;
								break;
							default:
								add_char_to_string(str, c, unlimited, 1, skip_mode, ctrl_l_buffer);
								if (oldpos < refline->length-1 && !insert)
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
							if(skip_mode)
								add_char_to_string(str, 0xFF, unlimited, 0, 0, NULL);  /* 0xFF is a special case; it means terminate input but don't add a \n at the end */
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
						add_char_to_string(str, c, unlimited, !literal, skip_mode, ctrl_l_buffer);
					}
					else
					{
						add_char_to_string(str, c, unlimited, !literal, skip_mode, ctrl_l_buffer);
					}
			}
		}
	} while(!stop);
	//bg_string(str);
	if(ctrl_l_buffer)
		finish_l_buffer(&ctrl_l_buffer, state);
	add_char_to_string(str, '\0', 1, 0, 0, NULL);
	str->length--;
	free_string(refline);
	return del_line;
}
struct string *get_lines_from_file(FILE *file, int *num_lines)
/* Reads all (remaining) lines from the give FILE and places them into a newly-allocated string vector. <num_lines> is set to the number of lines read */
{
	struct string *input_lines = NULL;
	*num_lines = 0;
	struct string buffer = {0,0,NULL};
	while(1)
	{
		buffer.length = getline(&buffer.buf, &buffer.space, file);
		if(buffer.length <= 0)
			break;
		(*num_lines)++;
		input_lines = realloc(input_lines, (*num_lines)*sizeof(struct string));
		input_lines[*num_lines-1] = buffer;
		buffer.buf = NULL;
		buffer.length = 0;
		buffer.space = 0;
	}
	return input_lines;
}
int main_buffer_text_entry(int insert_point, int num_lines_replaced, int edit_mode, int type_out, struct state_spec *state)
/* Gets text from the user for APPEND/INSERT/CHANGE/MODIFY/EDIT and puts it in the main buffer at the specified location <insert_point>. If <num_lines_replaced> is nonzero, the referenced lines are used as the "original line" in edit mode, and are replaced in CHANGE/MODIFY/EDIT. If type_out is true, the original lines are each typed out (used for EDIT mode). */
{
	int input_line_n = 0;
	int ref_line_n = insert_point+1;
	int last_line = insert_point+num_lines_replaced+1;
	struct string *input_lines = malloc(sizeof(struct string));
	struct string *ref_line = NULL;
	empty_string(input_lines);
	struct string *current_in_line = &input_lines[input_line_n];
	while(1)
	{
		if(edit_mode && ref_line_n >= last_line)
			break;
		if(edit_mode)
			ref_line = &(state->main_buffer[ref_line_n]);
		/* If the previous input line is complete (ends with a newline), we need to start a new input line */
		if(string_ends_with_char(current_in_line, '\n'))
		{
			input_line_n++;
			input_lines = realloc(input_lines, (input_line_n+1)*sizeof(struct string));
			current_in_line = &input_lines[input_line_n];
			empty_string(current_in_line);
		}
		if(type_out && ref_line)
		{
			for(int i=0; i<ref_line->length; i++) {print_char(ref_line->buf[i]);}
		}
		int d = get_string(current_in_line, '\0', 1, 1, 0, 1, ref_line, state);
		if (d)
		{
			delete_string(current_in_line);
			if(input_line_n > 0)
			{
				input_line_n--;
				ref_line_n--;
			}
			current_in_line = &input_lines[input_line_n];
			empty_string(&input_lines[input_line_n]);
		}
		else
		{
			/* Check for ctrl-d (end of input) */
			if (string_starts_with_char(current_in_line, 0x04))
			{
				input_line_n--;
				break;
			}
			else if (string_ends_with_char(current_in_line, 0x04))
			{
				current_in_line->buf[current_in_line->length-1] = '\n';
				printf("\r\n");
				break;
			}
			else if(string_ends_with_char(current_in_line, 0xFF))
			{
				string_backspace(current_in_line);
				printf("\r\n");
				break;
			}
			else if (string_ends_with_char(current_in_line, '\r'))
				current_in_line->buf[current_in_line->length-1] = '\n';
			ref_line_n++;
		}
	}
	if(input_line_n >= 0)
		current_in_line = &input_lines[input_line_n];
	input_line_n++;  /* Go from index of last element to number of elements */
	/* If the last line of input had its newline suppressed via Ctrl-K, we need to merge it with the next line in the main buffer (if there is one) */
	if(input_line_n > 0 && !string_ends_with_char(current_in_line, '\n'))
	{
		if(last_line > state->dollar)
			add_char_to_string(current_in_line, '\n', 1, 0, 0, NULL);
		else
		{
			cat_strings(current_in_line, &state->main_buffer[last_line]);
			num_lines_replaced++;
		}
	}
	/* Add the collected lines to the main buffer */
	state->dollar++;
	state->main_buffer = replace_elements_in_string_vector(state->main_buffer, &state->dollar, input_lines, input_line_n, insert_point+1, num_lines_replaced);
	state->dollar--;
	state->dot = insert_point+input_line_n;
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
	bzero(&command->arg1, sizeof(struct string));
	bzero(&command->arg2, sizeof(struct string));
	command->flag = 'G';
	command->num = -1;
	line = &(command->start);
	printf("*");
	do
	{
		int s;
		if(!(s = next_char(&c, 0, 0, 0, state)))
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
		else if(c == '+' || c == '-' || c == ' ')
		{
			if(*line == NULL || !compound_valid)
			{
				free_command_spec(command);
				return NULL;
			}
			line = &((*line)->next);
			*line = new_line_spec(c==' '?'+':c,'c',0,NULL);
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
			}
			else if((*line)->type == 'c')
			{
				(*line)->type = c;
			}
			else
			{
				line = &((*line)->next);
				*line = new_line_spec('+',c,0,NULL);
			}
			get_string(&((*line)->search), c=='['?']':c, 0, c=='[', 0, 1, NULL, state);
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
					if(!command->arg1.buf)
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
					get_string(&(command->arg1), c, 0, 1, 0, 1, NULL, state);
				}
				else if(c == 'S')
				{
					c = get_flags(command, state);
					if(!c)
					{
						free_command_spec(command);
						return NULL;
					}
					get_string(&(command->arg1), c, 0, 1, 0, 1, NULL, state);
					if (!state->quick)
					{
						printf(" FOR ");
						print_char(c);
					}
					get_string(&(command->arg2), c, 0, 1, 0, 1, NULL, state);
					if(command->arg2.length == 0)
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
			struct string buf;
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
			case '[':
			set_buffer(0, &line->search, state);
			if(!(line_number = find_string(&line->search, first?state->dot+1:line_number, line->type == ':', state))) {return -1;}
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
	char *sep = "\r";  /* Line separator used when printing lines; the P command will alter it depending on the user's response to DOUBLE? */
	/* Take the line spec for the start address, e.g. 3+4[foo], and resolve it to the actual line it refers to */
	if(command->start)
	{
		line1 = resolve_line_spec(command->start, state);
		if(line1 == -1)
		{
			err(state);
			return 0;
		}
	}
	/* For most commands, line 0 should be taken to mean line 1 (there is no actual line 0) */
	if(line1 == 0 && command->command != 'A' && command->command != '=' && command->command != 'R')
		line1 = 1;
	if(command->end)
	{
		line2 = resolve_line_spec(command->end, state);
		if(line2 == -1)
		{
			err(state);
			return 0;
		}
	}
	else
		line2 = line1;
	if(line2 == 0 && command->command != 'A' && command->command != '=' && command->command != 'R')
		line2 = 1;
 	if(command->command == '\n' && !command->start) 
		line2 = ++line1;
	/* Error out if end line is before start line, or if end line is past the end of the file */
	if(!strchr(cmd_noaddr, command->command) && (line2 < line1 || line2 > state->dollar))
	{
		err(state);
		return 0;
	}
	if(command->command != '=' && command->command != '<' && command->command != '\n')
		printf("\r\n");
	switch(command->command)
	{
		int i, n, num_lines, done;
		char c;
		struct string buffer;
		struct string *input_lines;
	case '^':
		if(state->dot <= 1)
		{
			err(state);
			return 0;
		}
		state->dot = state->dot - 1;
		print_string(&state->main_buffer[state->dot]);
		break;
	case '=':
		printf("%i\r\n", line1);
		break;
	case 'P':
		printf("\r\nDOUBLE? ");
		next_char(&c, 1, 1, 0, state);
		if(c == 'Y')
		{
			sep = "\r\n";
			printf("ES");
		}
		else if(c == 'N')
		{
			sep = "";
			printf("O");
		}
		else
		{
			printf("\r\n");
			err(state);
			return 0;
		}
		printf("\r\n");
	/* Intentional fallthrough */
	case '/':
	case '\n':
		for(i=line1; i<=line2; i++)
		{
			print_string(&state->main_buffer[i]);
			printf("%s", sep);
		}
		state->dot = line2;
		break;
	case 'I':
		if(!command->start)
			line1 = state->dot;
		main_buffer_text_entry(line1-1, 0, 0, 0, state);
		break;
	case 'A':
		if(!command->start)
			line1 = state->dollar;
		main_buffer_text_entry(line1, 0, 0, 0, state);
		break;
	case 'C':
		main_buffer_text_entry(line1-1, line2-line1+1, 0, 0, state);
		break;
	case 'E':
		main_buffer_text_entry(line1-1, line2-line1+1, 1, 1, state);
		break;
	case 'M':
		main_buffer_text_entry(line1-1, line2-line1+1, 1, 0, state);
		break;
	case 'L':
	case 'G':
		int buffer_length = 1; //length for the newlines and 1 for the terminating \0
		for (i=line1; i<=line2; i++)
		{
			buffer_length += state->main_buffer[i].length;
		}
		buffer.buf = malloc(buffer_length+1);
		buffer.space = buffer_length;
		buffer.length = 0;
		buffer.buf[0] = '\0';
		for(int i=line1; i<=line2; i++)
		{
			cat_strings(&buffer, &state->main_buffer[i]);
			buffer.buf[buffer.length-1] = '\r';
		}
		set_buffer(buffer_for_char(command->arg1.buf[0]), &buffer, state);
		if (command->command == 'L')
			break;
		/* Intentional fallthrough to 'D' if command was 'G' */
	case 'D':
		state->dollar++;
		state->main_buffer = replace_elements_in_string_vector(state->main_buffer, &state->dollar, NULL, 0, line1, line2-line1+1);
		state->dollar--;
		state->dot = line1-1;
		break;
	case 'R':
		if(!(state->file = fopen(command->arg1.buf, "r")))
		{
			err(state);
			printf("I-O ERROR.\r\n");
			return 0;
		}
		if(!command->start)
			line1 = state->dollar;
		line1++;
		input_lines = get_lines_from_file(state->file, &num_lines);
		/* Calculate and output the number of "words" read (i.e. the number of words of memory the SDS-930 would have used to store the text) */
		int num_bytes = 0;
		for (int i = 0; i < num_lines; i++)
		{
			num_bytes += input_lines[i].length;
		}
		int num_words = num_bytes / 3;
		if (num_bytes % 3)
			num_words++;
		state->dollar++;
		state->main_buffer = replace_elements_in_string_vector(state->main_buffer, &state->dollar, input_lines, num_lines, line1, 0);
		state->dollar--;
		state->dot = line1 + num_lines - 1;
		fclose(state->file);
		state->file = NULL;
		printf("%i WORDS.\r\n", num_words);
		break;
	case 'W':
		if(!(state->file = fopen(command->arg1.buf, "w")))
		{
			err(state);
			printf("I-O ERROR.\r\n");
			return 0;
		}
		if(!(command->start || command->end))
		{
			line1 = 1;
			line2 = state->dollar;
		}
		int bytes_written = 0;
		for(i = line1; i <= line2; i++)
		{
			fprintf(state->file, "%s", state->main_buffer[i].buf);
			bytes_written += state->main_buffer[i].length;
		}
		fclose(state->file);
		state->file = NULL;
		int words_written = bytes_written/3;
		if (bytes_written%3)
			words_written++;
		printf("%i WORDS.\r\n", words_written);
		break;
	case 'S':
		n = substitute(&command->arg1, &command->arg2, line1, line2, command->flag, command->num, state);
		if(n == 0)
			err(state);
		else
			printf("%i\r\n", n);
		break;
	case 'J':
		get_string(&buffer, '\0', 1, 1, 0, 0, NULL, state);
		//dbg_string(&buffer);
		buffer.length--;
		buffer.buf[buffer.length] = '\0';
		if(buffer.length > 0 && buffer.buf[buffer.length-1] != '\r')
			printf("\r\n");
		set_buffer(buffer_for_char(command->arg1.buf[0]), &buffer, state);
		break;
	case 'K':
		kill_buffer(buffer_for_char(command->arg1.buf[0]), state);
		break;
	case 'B':
		if(state->aux_buffers[buffer_for_char(command->arg1.buf[0])].buf)
		{
			printf("\"");
			print_string(&state->aux_buffers[buffer_for_char(command->arg1.buf[0])]);
			printf("\"\r\n");
		}
		break;
	case 'V':
		cmd_strings = cmd_strings_verbose;
		state->quick = 0;
		break;
	case 'Q':
		cmd_strings = cmd_strings_quick;
		state->quick = 1;
		break;
	case 'F':
			return 1;
	default:
		printf("[not implemented yet]\r\n");
	}
	state->wrote_out = (command->command == 'W');
	return 0;
}
void dump_state(struct state_spec *state)
{
	FILE *statefile;
	if (!(statefile = fopen(dumpfile, "w")))
	{
		err(state);
		return;
	}
	// Write signature
	fwrite("QED", 1, 3, statefile);
	// Write dumpfile format revision number
	fwrite(&dumprev, 1, sizeof(int), statefile);
	// Write dot and dollar
	fwrite(&state->dot, 1, sizeof(int), statefile);
	fwrite(&state->dollar, 1, sizeof(int), statefile);
	fwrite(&state->quick, 1, sizeof(int), statefile);
	// Write the aux buffers
	for(int i=0; i<NUM_AUX_BUFS; i++)
	{
		fwrite(&state->aux_buffers[i].length, 1, sizeof(int), statefile);
		if (state->aux_buffers[i].length > 0)
		{
			fwrite(state->aux_buffers[i].buf, state->aux_buffers[i].length, 1, statefile);
		}
	}
	for(int i = 1; i <= state->dollar; i++)
	{
		fwrite(state->main_buffer[i].buf, state->main_buffer[i].length, 1, statefile);
	}
}
struct state_spec* restore_state()
{
	FILE *statefile;
	if (!(statefile = fopen(dumpfile, "r")))
	{
		printf("IO-ERROR.\r\nCould not read continue file at %s\r\n", dumpfile);
		return NULL;
	}
	char *sig = malloc(4);
	bzero(sig, 4);
	fread(sig, 3, 1, statefile);
	if (strcmp(sig, "QED"))
	{
		printf("Invalid signature in continue file\r\n");
		return NULL;
	}
	int f_rev = 0;
	fread(&f_rev, 1, sizeof(int), statefile);
	struct state_spec *state = malloc(sizeof(struct state_spec));
	//printf("%i\r\n", f_rev);
	if(f_rev != dumprev)
	{
		printf("Incorrect continue file version (found %i, expected %i)\r\n", f_rev, dumprev);
		return NULL;
	}
	fread(&state->dot, 1, sizeof(int), statefile);
	fread(&state->dollar, 1, sizeof(int), statefile);
	fread(&state->quick, 1, sizeof(int),statefile);
	state->aux_buffers = calloc(sizeof(struct string), NUM_AUX_BUFS);
	for(int i=0; i < NUM_AUX_BUFS; i++)
	{
		int bsize = 0;
		fread(&bsize, 1, sizeof(int), statefile);
		//printf("B%i: %i bytes\r\n", i, bsize);
		string_with_capacity(&state->aux_buffers[i], bsize);
		if (!bsize)
		{
			continue;
		}
		read_string_from_file(&state->aux_buffers[i], bsize, statefile);
		if (state->aux_buffers[i].length < bsize)
		{
			printf("EOF encountered while loading buffer %i from continue file(expected %i, got %i)\r\n", i, bsize, state->aux_buffers[i].length);
			return NULL;
		}
	}
	state->file = statefile;
	state->main_buffer = calloc(sizeof(struct string), 1);
	int input_length = 0;
	struct string *lines = get_lines_from_file(state->file, &input_length);
	fclose(statefile);
	state->file = NULL;
	state->dollar = 1;
	state->main_buffer = replace_elements_in_string_vector(state->main_buffer, &state->dollar, lines, input_length, 1, 0);
	state->dollar -= 1;
	state->wrote_out = 1;
	state->buffer_stack = NULL;
	return state;
}
struct string *new_string()
/* Allocates and clears a new null string */
{
	struct string *s = malloc(sizeof(struct string));
	s->length = 0;
	s->space = 0;
	s->buf = NULL;
	return s;
}
struct string *empty_string(struct string *s)
/* Initializes a string to be empty but have space for characters to be added. Will allocate a new string if s is NULL. Returns the string either way */
{
	if (!s)
		s = new_string();
	s->length = 0;
	s->space = BUF_INCREMENT;
	s->buf = malloc(BUF_INCREMENT);
	return s;
}
struct string *string_with_capacity(struct string *s, size_t space)
/* Creates a blank string with allocated capacity for the specified number of characters. One byte is added to hold the terminating \0. If s is null, a new string is allocated. Return value is s or the new string */
{
	if(!s)
		s = new_string();
	s->length = 0;
	s->buf = malloc(space+1);
	s->buf[0] = '\0';
	s->space = space;
	return s;
}
struct string *string_from_cstring(struct string *s, char *cs)
/* Copies the contents of the c string cs into the string s. A string will be allocated if s is NULL. Any existing contents of s will be deleted. Returns s or the new string */
{
	if(!s)
		s = new_string();
	if(s->buf)
		free(s->buf);
	int l = strlen(cs);
	s->length = s->space = l;
	s->buf = malloc(l+1);
	strncpy(s->buf, cs, l);
	return s;
}
struct string *capture_cstring(struct string *s, char *cs, size_t space)
/* Captures the c-string cs, making s its owner. A new string will be allocated if s is NULL. Returns s or the new string. If space is non-zero, it will be used for the "space" parameter for s (i.e. the allocation length-1); otherwise the length of cs will be assumed for this. If a non-zero value of space is provided, it is up to the caller to make sure it is accurate */
{
	if (!s)
		s = new_string();
	if(s->buf)
		free(s->buf);
	s->buf = cs;
	s->length = strlen(cs);
	s->space = space?space:s->length;
	return s;
}
void delete_string(struct string *s)
/* Deletes and frees the contents of s and leaves it as a null string */
{
	if(!s)
		return;
	if(s->buf)
		free(s->buf);
	s->buf = NULL;
	s->space = 0;
	s->length = 0;
}
void free_string(struct string *s)
/* Deletes and frees the contents of s, if any, then frees s */
{
	if (!s)
		return;
	if (s->buf)
		free(s->buf);
	free(s);
}
struct string *copy_string(struct string *dst, struct string *src, int copy_space)
/* Copies the contents of src to dst, replacing existing contents. If copy_space is true, dst will be left with as much extra space as src had. If dst is NULL, a new string will be allocated. Returns dst or the new string */
{
	if (!dst)
		dst = new_string();
	size_t dst_space = copy_space?src->space:src->length;
	if(dst->buf)
		free(dst->buf);
	dst->buf = malloc(dst_space+1);
	memcpy(dst->buf, src->buf, src->length+1);
	dst->length = src->length;
	dst->space = dst_space;
	return dst;
}
struct string *cat_slice(struct string *dst, struct string *src, int start, int length)
/* Copies a segment of the string src to the end of dst. If dst is NULL, a new string will be allocated. Returns dst or the new string. Start is the index of the first char to be copied, length is the number of chars to copy. If length is negative, the copy will be to the end of src. The copy will be trimmed to the length of src. */
{
	if (!dst)
		dst = new_string();
	if (start >= src->length)
		return dst;
	int max_length = src->length - start;
	int cpy_length;
	if (length < 0 || length > max_length)
		cpy_length = max_length;
	else
		cpy_length = length;
	size_t space_needed = src->length + cpy_length;
	if (space_needed > dst->space)
	{
		dst->buf = realloc(dst->buf, space_needed+1);
		dst->space = space_needed;
	}
	memcpy(dst->buf+dst->length, src->buf+start, cpy_length);
	dst->length += cpy_length;
	dst->buf[dst->length] = '\0';
	return dst;
}
void cat_strings(struct string *s1, struct string *s2)
/* Concatenates two strings. The string s1 is modified by adding a copy of the contents of s2 to the end. The string s2 is not changed. */
{
	int req_len = s1->length + s2->length;
	if (s1->space < req_len)
	{
		s1->buf = realloc(s1->buf, req_len+1);
		s1->space = req_len;
	}
	memcpy(s1->buf+s1->length, s2->buf, s2->length);
	s1->buf[req_len] = '\0';
	s1->length = req_len;
}
int print_string(struct string *s)
{
	if (!s)
		return 0;
	return print_buffer(s->buf);
}
int string_starts_with_char(struct string *s, char c)
/* Returns 1 if the string s exists and starts with the specified character */
{
	return s && s->length && s->buf[0] == c;
}
int string_ends_with_char(struct string *s, char c)
/* Returns 1 if the string s exists and ends with the specified character */
{
	return s && s->length && s->buf[s->length-1] == c;
}
struct string *read_string_from_file(struct string *s, int length, FILE *f)
/* Reads a string of length <length> into the string s from the file f. If s is NULL a new string will be allocated. The capacity of s will be expanded if needed. Returns s or the new string. If the file reached EOF before <length> bytes were read, s may be smaller then <length> */
{
	if (!s)
		s = new_string();
	if (s->space < length)
	{
		s->buf = realloc(s->buf, length+1);
		s->space = length;
	}
	s->length = fread(s->buf, 1, length, f);
	s->buf[s->length] = '\0';
	return s;
}
void string_backspace(struct string *s)
/* Deletes the last char from the string s. No effect if s is null or empty */
{
	if(!s || !s->buf || !s->length)
		return;
	s->length--;
	s->buf[s->length] = '\0';
}
