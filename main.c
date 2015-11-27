#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>


const char up_arrow[4] = {0xE2, 0x86, 0x91, 0x00};
const char left_arrow[4] = {0xE2, 0x86, 0x90, 0x00};
const char *addr_chars = "0123456789,;.&+- ";
const char *cmd_chars = "\"/=^<\n\rABCDEFGIJKLMPQRSTVW";
char *cmd_strings[26] = {"\"", "/", "=", "↑", "←", "\r\n", "\r\n", "APPEND", "BUFFER", "CHANGE", "DELETE", "EDIT", "FINISHED", "GET", "INSERT", "JAM INTO", "KILL", "LOAD", "MODIFY", "PRINT", "QUICK", "READ FROM", "SUBSTITUTE", "TABS", "VERBOSE", "WRITE ON"};
const char *cmd_noconf = "\"/=^<\n\r";
const int BUF_INCREMENT = 30;

struct command_spec {
	struct line_spec *start;
	struct line_spec *end;
	char command;
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
void get_string(char **str, int *length, char delim, int full, int unlimited);
struct command_spec* qed_getline();
int increase_buffer(char **buffer, size_t *size);
struct line_spec *new_line_spec(char sign, char type, int line, char *search);
void free_command_spec(struct command_spec *cmd);
int main(int argc, char **argv)
{
	struct command_spec *command;
	int finished = 0;
	struct termios qed_term_settings;
	struct termios original_term_settings;
	tcgetattr(fileno(stdin), &original_term_settings);
	qed_term_settings = original_term_settings;
	cfmakeraw(&qed_term_settings);
	tcsetattr(fileno(stdin), TCSANOW, &qed_term_settings);

	do
	{
		command = qed_getline();
		if(command != NULL)
		{
			/* TODO: Actually do the command */
			finished = (command->command == 'F');
			free_command_spec(command);
		}
		else
		{
			err();
		}
		printf("\r\n");
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
void get_string(char **str, int *length, char delim, int full, int unlimited)
{
	char c;
	int newstr = 1;
	int stop = 0;
	int buf_size = BUF_INCREMENT;
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
					add_char_to_buffer(str, length, &buf_size, c, unlimited);
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
		else if(c == '.' || c == '&')
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
			cmd_str = cmd_strings[cmd_char_index];
	 		printf("%s", cmd_str);
			done = 1;
			command->command = c;
			if(!strchr(cmd_noconf, c))
			{
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
