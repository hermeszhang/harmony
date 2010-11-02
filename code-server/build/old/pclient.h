#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>


#include "hutil.h"
#include "hmesgs.h"
#include "hsockutil.h"
#include "StringTokenizer.h"

/***
 * macro definitions
 ***/

#define SERVER_PORT 1977

/*
 * the size of the buffer used to transfer data between server and client
 */
#define BUFFER_SIZE 1024

#define MAX_VAR_NAME BUFFER_SIZE

/*
 * define truth values
 */
#define false 0
#define true 1


//char* global_projected = NULL;

/***
 * function prototypes
 ***/
void projection_startup();

/*
 * When a program announces the server that it will end it calls this function
 */
void projection_end();

/*
 * Is given point admissible?
*/
int is_a_valid_point(char* point);

/*
 * simplex construction:: returns the simplex
 */
char* simplex_construction(char* request);

/*
 * projection: project a point to the admissible region
 */
string do_projection(char *request);

char* projection_sim_construction_2(char* request, int mesg_type);

char* string_to_char_star(string str);





