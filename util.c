#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "util.h"

/*
 * Reads a line
 */
char *read_line(FILE *ptr) {
    char *line = malloc(128), *linep = line;
    size_t lenmax = 128, len = lenmax;
    int c;

    if (line == NULL) {
        return NULL;
    }

    for (;;) {
        c = fgetc(ptr);
        
        // end of string / file => return string
        if(c == EOF) {
            break;
        }

        // string buffer full => double the size
        if (--len == 0) {
            len = lenmax;
            char * linen = realloc(linep, lenmax *= 2);

            if (linen == NULL) {
                free(linep);
                return NULL;
            }
            
            line = linen + (line - linep);
            linep = linen;
        }

        // end of line => return string
        if((*line++ = c) == '\n') {
            break;
        }
    }

    *line = '\0';
    
    // nothing read => return NULL
    if (strlen(linep) == 0) {
        return NULL;
    }
    
    // remove newline character at the end of the string
    if (*(line - 1) == '\n') {
        *(line - 1) = '\0';
        
        // special case Windows (DOH!) -> remove \r as well
        if (*(line - 2) == '\r') {
            *(line - 2) = '\0';
        }
    }
    
    return linep;
}

/*
 * Turns non alpha characters into spaces
 */
void nonalpha_to_space(char *str) {
    char *c;
    for(c = str; *c; c++) {
        if(!isalpha(*c)) {
            *c = ' ';
        } else {
            *c = tolower(*c);
        }
    }
}

/*
 * Checks if pre is a prefix of str
 */
int starts_with(char *str, char *pre) {
    return strncmp(pre, str, strlen(pre)) == 0;
}
