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
char convert_esc(char c);
int qed_getline(char **buffer, size_t *size);
int increase_buffer(char **buffer, size_t *size);
int main(int argc, char **argv)
{
	size_t ibuffer_size = BUF_INCREMENT;
	char *ibuffer = malloc(BUF_INCREMENT);
	struct termios qed_term_settings;
	struct termios original_term_settings;
	int getline_result = 0;
	tcgetattr(fileno(stdin), &original_term_settings);
	qed_term_settings = original_term_settings;
	cfmakeraw(&qed_term_settings);
	tcsetattr(fileno(stdin), TCSANOW, &qed_term_settings);

	getline_result = qed_getline(&ibuffer, &ibuffer_size);
	printf("\rGot Command: \"%s\"%s\n", ibuffer, getline_result?"":" (cancelled)");

	tcsetattr(fileno(stdin), TCSANOW, &original_term_settings);
	return 0;
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
int qed_getline(char **buffer, size_t *size)
{
	int i=0;
	unsigned char c = '\0';
	int done = 0;
	char *cmd_char_ptr = NULL;
	int return_value = 0;
	printf("*");
	do
	{
		if(i == *size - 1)
		{
			increase_buffer(buffer, size);
		}
		if(!scanf("%c", &c))
		{
			done = 1;
			return_value = 0;
		}
		c = convert_esc(c);
		if(strchr(addr_chars, c))
		{
			printf("%c", c);
			(*buffer)[i] = c;
		}
		else if((cmd_char_ptr = strchr(cmd_chars, c)))
		{
			int cmd_char_index = (int)(cmd_char_ptr - cmd_chars);
			char *cmd_str = cmd_strings[cmd_char_index];
	 		printf("%s", cmd_str);
			(*buffer)[i] = c;
			if(strchr(cmd_noconf, c))
			{
				done = 1;
				return_value = 1;
			}
			else
			{
				scanf("%c",&c);
				c = convert_esc(c);
				done = 1;
				return_value = (c == '.');
				return_value = c == '.'?1:0;
				printf("%c", c);
			}
		}
		else {printf("%c", 0x07);}
		i++;
	} while(!done);
	(*buffer)[i] = '\0';
	printf("\n");
	return return_value;
}
