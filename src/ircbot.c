#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <inttypes.h>
#include <errno.h>

//Global Commands
#define testCommand ":!testCommand"
#define quitCommand ":!quit"
#define joinCommand ":!join"
#define leaveCommand ":!leave"
#define joinCommand ":!join"
#define helpCommand ":!help"
#define commandsCommand ":!commands"
#define sourceCommand ":!source"
#define versionCommand ":!version"
#define addGlobalUserCommand ":!addGlobalUser"
#define addChannelUserCommand ":!addChannelUser"
#define addChannelCommand ":!addChannel"
#define addChannelCommandCommand ":!addCommand"
#define updateAddChannelCommandPrivilegeLevelCommand ":!updateAddChannelCommandPrivilegeLevel"

//Structs
typedef struct User{
	char username[256];
	int privilegeLevel;
}User;

typedef struct Command{
	int privilegeLevel;
	char message[256];
}Command;

typedef struct Channel{
	char channelName[256];
	int addCommandPrivilegeLevel;
	int numCommands;
	int maxCommands;
	Command* commands;
	int numUsers;
	int maxUsers;
	User* users;
}Channel;



//The name of this bot
char username[256];

//The socket connection (Global for convienence)
int sockfd;

Channel* channels;

int numChannels = 0;

int maxChannels = 0;

User* globalUsers;

int numGlobalUsers;

int maxGlobalUsers;

//Handles an irc message and returns a response when necasary
int handleMessage(char *message);

//Reads in an irc message from the socket
int readMessage(char* buffer, int buffSize);

//Sends an irc message through the socket
int sendMessage(const char* command, const char* target, const char* message, char multiWord);

int initializeChannelAndUserLists(const char* fileName);

int resizeGlobalUsers();

int resizeChannelList();

User* resizeGenericUserList(User* users, int* curMaxSize);

Command* resizeGenericCommandsList(Command* commands, int* curMaxSize);

int addGlobalUser(const char* newUsername, int privilegeLevel);

int addChannelUser(Channel* channel, const char* newUsername, int privilegeLevel);

int addChannel(const char* channel);

Channel* getChannel(const char* channel);

int checkGlobalPrivilege(const char* user);

int checkChannelPrivilege(Channel* channel, const char* user);

void tryChannelCommand(const char* channel, const char* command, const char* user);

void error(const char *msg)
{
	perror(msg);
	exit(0);
}

int main(int argc, char *argv[])
{
	int portno, n;
	struct sockaddr_in serv_addr;
	struct hostent *server;
	char password[256];
	FILE *fptr;
	char buffer[513];
	//Set up the cokect connection
	if (argc < 4) {
	   fprintf(stderr,"usage %s hostname port\n", argv[0]);
	   exit(0);
	}
	portno = atoi(argv[2]);
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0){
		error("ERROR opening socket");
	}
	server = gethostbyname(argv[1]);
	if (server == NULL) {
		fprintf(stderr,"ERROR, no such host\n");
		exit(0);
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, 
		 (char *)&serv_addr.sin_addr.s_addr,
		 server->h_length);
	serv_addr.sin_port = htons(portno);
	if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0){
		error("ERROR connecting");
	}
	
	//Retrieve account information
	if((fptr = fopen(argv[3], "r")) == NULL){
		perror("Error opening account file");
		return -1;
	}


	bzero(username, 256);
	bzero(password, 256);
	bzero(buffer, 513);

	while(fscanf(fptr, "%64s ", buffer) == 1){
		if(strcmp(buffer, "USERNAME") == 0){
			fscanf(fptr, "%64s ", username);
		}
		else if(strcmp(buffer, "PASSWORD") == 0){
			fscanf(fptr, "%64s ", password);
		}
	}
	fclose(fptr);

	if(strlen(username) == 0){
		strcpy(username, "bobjrseniorTest");
	}

	initializeChannelAndUserLists(NULL);
	
	//Initial connection messages

	if(strlen(password) > 0){
		
		//PASS
		n = sendMessage("PASS", "", password, 0);
		if (n < 0){
			 error("ERROR writing to socket");
		}
		bzero(password, 256);
		bzero(buffer, 513);
		password[0] = 1;
	}


	//NICK
	n = sendMessage("NICK", "", username, 0);
	if (n < 0){
		 error("ERROR writing to socket");
	}

	//You are a guest if you don't have a password
	if(strlen(password) == 0){
		//USER
		n = sendMessage("USER", "", "guest 0 * :bob", 0);
		if (n < 0){
			 error("ERROR writing to socket");
		}
	}
	//JOIN (#botters-test by default for now)
	n = sendMessage("JOIN", "", "#botjrsenior", 0);
	if (n < 0){
		 error("ERROR writing to socket");
	}

	//Loop until we recieve a quit command
	while(1){
		//Read the next message
		bzero(buffer,512);
		n = readMessage(buffer,512);
		if (n < 0){
			error("ERROR reading from socket");
			break;
		}
		//Handle the message and quit if we want to
		if(handleMessage(buffer) == 1){
			break;
		}
	}
	//Tell the socket that we are leaving
	n = sendMessage("QUIT", "", "Quitting Session", 0);
	if (n < 0){
		 error("ERROR writing to socket");
	}
	//Close the socket
	close(sockfd);
	return 0;
}

int handleMessage(char *message){
	char prefixBuff[512];
	char command[8];
	char target[512];
	char actualMessage[512];
	char delimitors[] = " "; 		
	printf("%s\n", message);

	char* token = NULL;
	int state = 0;
	
	//Messages starting with ':' have a prefix
	if(message[0] != ':'){
		state = 1;
	}

	//Get the first token
	//This is done before the loop because subsequent strtok calls will have NULL for the first param
	if((token = strtok(message, delimitors)) == NULL){
		printf("Bad IRC Line\n");
		return 0;
	}

	//Loop throught the message
	do{
		//If reading the prefix
		if(state == 0){
			strcpy(prefixBuff, token);
			++state;
		}//If reading the command
		else if(state == 1){
			strcpy(command, token);
			++state;
		}//If reading params
		else if(state == 2){
			//If it is a param that can have spaces
			if(token[0] == ':'){

				//Copy until the end of the message
				strcpy(actualMessage, token);
				
				while((token = strtok(NULL, delimitors)) != NULL){
					strcat(actualMessage, " ");
					strcat(actualMessage, token);
				}
				break;
			}//Param without spaces
			else{
				//Only one of these is currently supported at a time
				strcpy(target, token);
			}
		}
	}while((token = strtok(NULL, delimitors)) != NULL);

	//If this message is a PING, send a PONG
	if(strcmp(command, "PING") == 0){
		if(sendMessage("PONG", "", actualMessage, 0) < 0){
			error("Error sending message");
		}
	}//If it is a normal message
	else if(strcmp(command, "PRIVMSG") == 0){
		//Find the sender
		char* sender =  strtok(&prefixBuff[1], ":!");
		//If it was a pm, change the target to the user pming this bot
		if(strcmp(target, username) == 0){
			strcpy(target, sender);
		}
		//Commands must start with a '!' (ignoring leading : designating the parm
		if(actualMessage[1] == '!'){
			//If it is a testCommand, respong with "Hello <sender>"
			if(strncmp(actualMessage, testCommand, strlen(testCommand)) == 0){
				if(checkGlobalPrivilege(sender) <= 0){
					char sending[256];
					strcpy(sending, "Hello ");
					strcat(sending, sender);
					if(sendMessage("PRIVMSG", target, sending, 1) < 0){
						perror("Error sending message");
						return -1;
					}
				}
			}//If it is a quit command, signal we are done
			else if(strncmp(actualMessage, quitCommand, strlen(quitCommand)) == 0){
				if(checkGlobalPrivilege(sender) <= 0){
					return 1;
				}
			}
			else if(strncmp(actualMessage, leaveCommand, strlen(leaveCommand)) == 0){
				
				if(checkGlobalPrivilege(sender) <= 3){
					if(sendMessage("PART", "", target, 0) < 0){
						perror("Error sending message");
						return -1;
					}
				}
			}
			else if(strncmp(actualMessage, joinCommand, strlen(joinCommand)) == 0){
				
				if(checkGlobalPrivilege(sender) <= 3){
					if((token = strtok(actualMessage, delimitors)) != NULL){
					
						if((token = strtok(NULL, delimitors)) != NULL){
							if(sendMessage("JOIN", "", token, 0) < 0){
								perror("Error sending message");
								return -1;
							}
						}
					}
				}
			}
			else if(strncmp(actualMessage, helpCommand, strlen(helpCommand)) == 0){
			
				if(checkGlobalPrivilege(sender) <= 9){
					if(sendMessage("PRIVMSG", target, "Help come to those who help themself", 1) < 0){
						perror("Error sending message");
						return -1;
					}
				}
			}
			else if(strncmp(actualMessage, commandsCommand, strlen(commandsCommand)) == 0){
				
				if(checkGlobalPrivilege(sender) <= 9){
					if(sendMessage("PRIVMSG", target, "Available commands are !testCommand, !join <channel>, !leave, !quit, !source, !commands, !version, !addGlobalUser", 1) < 0){
						perror("Error sending message");
						return -1;
					}
				}
			}
			else if(strncmp(actualMessage, sourceCommand, strlen(sourceCommand)) == 0){
				
				if(checkGlobalPrivilege(sender) <= 9){
					if(sendMessage("PRIVMSG", target, "This bot's sourcecode is available at https://github.com/bobjrsenior/ircChatBot", 1) < 0){
						perror("Error sending message");
						return -1;
					}
				}
			}
			else if(strncmp(actualMessage, versionCommand, strlen(versionCommand)) == 0){
				
				if(checkGlobalPrivilege(sender) <= 9){
					if(sendMessage("PRIVMSG", target, "Not Available", 1) < 0){
						perror("Error sending message");
						return -1;
					}
				}
			}
			else if(strncmp(actualMessage, addGlobalUserCommand, strlen(addGlobalUserCommand)) == 0){
				
				if(checkGlobalPrivilege(sender) <= 0){
					
					if((token = strtok(actualMessage, delimitors)) != NULL){
						if((token = strtok(NULL, delimitors)) != NULL){
							char* privilegeChar;	
							int convertedPrivilege;
							if((privilegeChar = strtok(NULL, delimitors)) != NULL){
								convertedPrivilege = (int) strtoimax(privilegeChar, NULL, 10);
								int status;
								if(errno != ERANGE && (status = addGlobalUser(token, convertedPrivilege)) >= 0){
									if(sendMessage("PRIVMSG", target, (status == 0) ? "Global user added successfully" : "Global user updated successfully", 1) < 0){
										perror("Error sending message");
										return -1;
									}
									return 0;
								}
								errno = 0;
							}
						}
					}
					
					if(sendMessage("PRIVMSG", target, "Error while adding global user", 1) < 0){
						perror("Error sending message");
						return -1;
					}
				}
			}
			else if(strncmp(actualMessage, addChannelUserCommand, strlen(addChannelUserCommand)) == 0){
				Channel* channel = getChannel(target);
				
				if(channel != NULL && checkChannelPrivilege(channel, sender) <= channel->addCommandPrivilegeLevel){
					if(strtok(actualMessage, delimitors) != NULL && (token = strtok(NULL, delimitors)) != NULL){
						
						char* privilegeChar;	
						int convertedPrivilege;
						if((privilegeChar = strtok(NULL, delimitors)) != NULL){
							convertedPrivilege = (int) strtoimax(privilegeChar, NULL, 10);
							int status;
						
							if(errno == ERANGE || (status = addChannelUser(channel, token, convertedPrivilege)) < 0){
								perror("Error adding user");
								return -1;
							}
							
							if(sendMessage("PRIVMSG", target, (status == 0) ? "Channel user added successfully" : "Channel user updated", 1) < 0){
								perror("Error sending message");
								return -1;
							}
						}						
					}
				}
			}
			else if(strncmp(actualMessage, addChannelCommand, strlen(addChannelCommand)) == 0){
				if(checkGlobalPrivilege(sender) <= 3){
					if(strtok(actualMessage, delimitors) != NULL && (token = strtok(NULL, delimitors)) != NULL){
						char status;
						if((status = addChannel(token)) < 0){
							perror("Error adding channel");
							return -1;
						}
							
						Channel* channel = getChannel(token);
						if(channel == NULL || addChannelUser(channel, sender, 0) < 0){
							perror("Error adding channel");
							return -1;
						}
						
						if(sendMessage("PRIVMSG", target, (status == 0) ? "Channel added successfully" : "Channel Already exists", 1) < 0){
							perror("Error sending message");
							return -1;
						}
						
					}
					else{	
						char status;
						if((status = addChannel(target)) < 0){
							perror("Error adding channel");
							return -1;
						}
						Channel* channel = getChannel(target);
						if(channel == NULL || addChannelUser(channel, sender, 0) < 0){
							perror("Error adding channel");
							return -1;
						}
						
						if(sendMessage("PRIVMSG", target, (status == 0) ? "Channel added successfully" : "Channel Already exists", 1) < 0){
							perror("Error sending message");
							return -1;
						}
					}
				}
			}
			else if(strncmp(actualMessage, updateAddChannelCommandPrivilegeLevelCommand, strlen(updateAddChannelCommandPrivilegeLevelCommand)) == 0){
				Channel* channel = getChannel(target);
				printf("In Command\n");
				if(channel != NULL && checkChannelPrivilege(channel, sender) <= channel->addCommandPrivilegeLevel){
					printf("Found channel and have permission\n");
					if(strtok(actualMessage, delimitors) != NULL && (token = strtok(NULL, delimitors)) != NULL){
						printf("Tokenized: %s\n", token);	
						int convertedPrivilege;
						convertedPrivilege = (int) strtoimax(token, NULL, 10);
						
						if(errno == ERANGE){
							perror("Error converting privilege");
							return -1;
						}
						
						channel->addCommandPrivilegeLevel = convertedPrivilege;
						if(sendMessage("PRIVMSG", target, "Channel add command privilege updated successfully", 1) < 0){
							perror("Error sending message");
							return -1;
						}
						
					}
				}
			}
		}
	}
	return 0;
}

int readMessage(char* buffer, int buffSize){
	int cur = 0;
	int error = 0;
	//Loop until there is an error or we reach the end of the message/buffer
	do{
		//Read in the next character
		error = read(sockfd, &buffer[cur], 1);
	}while(error >= 0  && buffer[cur++] != '\n' && cur < buffSize);
	//If there was an error, return it
	if(error < 0){
		return error;
	}
	//End with a termination character
	buffer[cur] = '\0';
	return cur;
}

int sendMessage(const char* command, const char* target, const char* message, char multiWord){
	char buffer[512];
	//Message Format: command <target> :message
	buffer[0] = '\0';
	strcat(buffer, command);
	strcat(buffer, " ");
	//Only add a target if there is one
	if(target[0] != '\0'){
		strcat(buffer, target);
		strcat(buffer, " ");
	}
	//Only add a : to begin the param if we want one
	if(multiWord){
		strcat(buffer, ":");
	}
	strcat(buffer, message);
	int len = strlen(buffer);
	buffer[len++] = '\r';
	buffer[len++] = '\n';
	return write(sockfd, buffer, len);
}


int initializeChannelAndUserLists(const char* fileName){
	char mainAdmin[] = "bobjrsenior";

	//Initialize Global List
	numGlobalUsers = 1;
	maxGlobalUsers = 10;
	if((globalUsers = (User*) malloc(maxGlobalUsers * sizeof(User))) == NULL){
		return -1;
	}
	globalUsers[0].privilegeLevel = 0;
	strcpy(globalUsers[0].username, username);

	//Initialize Channel List
	numChannels = 0;
	maxChannels = 10;
	if((channels = (Channel*) malloc(maxChannels * sizeof(Channel))) == NULL){
		return -1;
	}


	return addGlobalUser(mainAdmin, 0);
}

int resizeGlobalUsers(){
	maxGlobalUsers *= 1.5;
	if((globalUsers = (User*) realloc(globalUsers, maxGlobalUsers * sizeof(User))) == NULL){
		return -1;
	}
	return 0;
}

int resizeChannelList(){
	maxChannels *= 1.5;
	if((channels = (Channel*) realloc(channels, maxChannels * sizeof(Channel))) == NULL){
		return -1;
	}
	return 0;
}

User* resizeGenericUserList(User* users, int* curMaxSize){
	*curMaxSize = *curMaxSize * 1.5;
	return (User*) realloc(users, *curMaxSize * sizeof(User));
}

Command* resizeGenericCommandsList(Command* commands, int* curMaxSize){
	*curMaxSize = *curMaxSize * 1.5;
	return (Command*) realloc(commands, *curMaxSize * sizeof(Command));
}

int addGlobalUser(const char* newUsername, int privilegeLevel){
	int e = 0;
	for(; e < numGlobalUsers; ++e){
		if(strcmp(newUsername, globalUsers[e].username) == 0){
			globalUsers[e].privilegeLevel = privilegeLevel;
			return 1;
		}
	}

	if(maxGlobalUsers == numGlobalUsers){
		if(resizeGlobalUsers() < 0){
			return -1;
		}
	}

	globalUsers[numGlobalUsers].privilegeLevel = privilegeLevel;
	strcpy(globalUsers[numGlobalUsers].username, newUsername);
	
	++numGlobalUsers;
	return 0;
}

int addChannelUser(Channel* channel, const char* newUsername, int privilegeLevel){
	int e = 0;
	for(; e < channel->numUsers; ++e){
		if(strcmp(newUsername, channel->users[e].username) == 0){
			channel->users[e].privilegeLevel = privilegeLevel;
			return 1;
		}
	}
	if(channels->numUsers == channel->maxUsers){
		if(resizeGenericUserList(channel->users, &channels->maxUsers) < 0){
			return -1;
		}
	}
	channel->users[channel->numUsers].privilegeLevel = privilegeLevel;
	strcpy(channel->users[channel->numUsers].username, newUsername);
	++channels->numUsers;
	return 0;
}

int addChannel(const char* channel){
	int e = 0;
	for(; e < numChannels; ++e){
		if(strcmp(channel, channels[e].channelName) == 0){
			return 1;
		}
	}

	if(maxChannels == numChannels){
		if(resizeChannelList() < 0){
			return -1;
		}
	}
	
	strcpy(channels[numChannels].channelName, channel);
	channels[numChannels].addCommandPrivilegeLevel = 0;
	channels[numChannels].numCommands = 0;
	channels[numChannels].maxCommands = 10;
	
	//Create commands	
	if((channels[numChannels].commands = (Command*) malloc(10 * sizeof(User))) == NULL){
		return -1;
	}

	channels[numChannels].numUsers = 0;
	channels[numChannels].maxUsers = 10;
	//Create Users
	if((channels[numChannels].users = (User*) malloc(10 * sizeof(User))) == NULL){
		return -1;
	}

	++numChannels;
	return 0;
}

Channel* getChannel(const char* channel){
	int e = 0;
	for(; e < numChannels; ++e){
		if(strcmp(channel, channels[e].channelName) == 0){
			return &channels[e];
		}
	}
	return NULL;
}

int checkGlobalPrivilege(const char* user){
	int e = 0;
	int global = numGlobalUsers;
	for(; e < global; ++e){
		if(strcmp(user, globalUsers[e].username) == 0){
			return globalUsers[e].privilegeLevel;
		}
	}
	return 9;
}

int checkChannelPrivilege(Channel* channel, const char* user){
	int e = 0;
	for(; e < channel->numUsers; ++e){
		if(strcmp(user, channel->users[e].username) == 0){
			return channel->users[e].privilegeLevel;
		}
	}

	return 9;
}

void tryChannelCommand(const char* channel, const char* command, const char* user){

}
