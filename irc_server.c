// server.c - Supports Private Messages, User Listing, Auth & Groups
// Compile: gcc irc_server.c chat_db.c -o server -pthread

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "chat_db.h" // Added for Auth & Groups

#define PORT 8080
#define MAX_CLIENTS 50

typedef struct {
    int socket;
    int id;
    int is_admin; // Server admin (first user)
    int room_id;  // 1=General, 2=Study, 3=Gaming, >=100 Custom Groups
    char name[50];
    int is_logged_in; // NEW: Auth State
} Client;

Client *clients[MAX_CLIENTS];
int uid_counter = 10;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- FILE HISTORY FUNCTIONS ---
void get_filename(int room_id, char *filename) {
    if (room_id == 1) strcpy(filename, "chat_general.txt");
    else if (room_id == 2) strcpy(filename, "chat_study.txt");
    else if (room_id == 3) strcpy(filename, "chat_gaming.txt");
    else {
        // For custom groups, we use a generic file or specific ID file
        sprintf(filename, "chat_group_%d.txt", room_id);
    }
}

void save_message_to_file(int room_id, const char *message) {
    pthread_mutex_lock(&file_mutex);
    char filename[50];
    get_filename(room_id, filename);
    
    FILE *f = fopen(filename, "a");
    if (f) {
        fprintf(f, "%s\n", message);
        fclose(f);
    }
    pthread_mutex_unlock(&file_mutex);
}

void send_history_to_client(int socket, int room_id) {
    pthread_mutex_lock(&file_mutex);
    char filename[50];
    get_filename(room_id, filename);
    
    FILE *f = fopen(filename, "r");
    if (f) {
        char line[2048];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\n")] = 0;
            send(socket, line, strlen(line), 0);
            usleep(1000); 
        }
        fclose(f);
    }
    pthread_mutex_unlock(&file_mutex);
}

// --- NETWORK FUNCTIONS ---
void send_to_room(char *message, int room_id, int sender_sock) {
    pthread_mutex_lock(&clients_mutex);
    
    // Save history
    save_message_to_file(room_id, message);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        // Send to everyone in room, who is also LOGGED IN
        if (clients[i] && clients[i]->room_id == room_id && 
            clients[i]->socket != sender_sock && clients[i]->is_logged_in) {
            send(clients[i]->socket, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int sock) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->socket == sock) {
            char leave_msg[100];
            sprintf(leave_msg, "SERVER:%s has left the chat.", clients[i]->name);
            printf("%s (Room %d)\n", leave_msg, clients[i]->room_id);
            
            int room = clients[i]->room_id;
            
            pthread_mutex_unlock(&clients_mutex);
            send_to_room(leave_msg, room, sock);
            pthread_mutex_lock(&clients_mutex);

            free(clients[i]);
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg) {
    Client *cli = (Client *)arg;
    char buffer[2048];
    int n;

    // Receive initial connection Name (from Client UI)
    if (recv(cli->socket, cli->name, 50, 0) <= 0) {
        remove_client(cli->socket);
        close(cli->socket);
        return NULL;
    }

    cli->room_id = 1; 
    
    // NOTE: We do NOT send history or join message yet. 
    // User must login first.

    while ((n = recv(cli->socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[n] = '\0';
        char formatted_msg[4096];

        // ======================================================
        // NEW FEATURE: AUTHENTICATION GATEKEEPER
        // ======================================================
        if (!cli->is_logged_in) {
            char cmd[20], u[50], p[50];
            // Expecting: /login user pass OR /register user pass
            if (sscanf(buffer, "%s %s %s", cmd, u, p) == 3) {
                if (strcmp(cmd, "/login") == 0) {
                    if (login_user(u, p)) {
                        cli->is_logged_in = 1;
                        strcpy(cli->name, u); // Adopt the authenticated name
                        send(cli->socket, "SERVER: Login successful.\n", 26, 0);
                        
                        // NOW we do the join logic
                        send_history_to_client(cli->socket, 1);
                        char join_msg[100];
                        sprintf(join_msg, "SERVER:%s joined General Channel.", cli->name);
                        send_to_room(join_msg, 1, cli->socket);
                    } else {
                        send(cli->socket, "SERVER: Invalid credentials.\n", 29, 0);
                    }
                } 
                else if (strcmp(cmd, "/register") == 0) {
                    if (register_user(u, p)) {
                        cli->is_logged_in = 1;
                        strcpy(cli->name, u);
                        send(cli->socket, "SERVER: Registered & Logged in.\n", 32, 0);
                        
                        send_history_to_client(cli->socket, 1);
                        char join_msg[100];
                        sprintf(join_msg, "SERVER:%s joined General Channel.", cli->name);
                        send_to_room(join_msg, 1, cli->socket);
                    } else {
                        send(cli->socket, "SERVER: Username taken.\n", 24, 0);
                    }
                } else {
                    send(cli->socket, "SERVER: Please use /login [user] [pass] or /register [user] [pass]\n", 67, 0);
                }
            } else {
                send(cli->socket, "SERVER: Auth required. Use /login [u] [p] or /register [u] [p].\n", 64, 0);
            }
            continue; // Stop here, don't process other commands
        }

        // ======================================================
        // NEW FEATURE: GROUP MANAGEMENT COMMANDS
        // ======================================================
        if (strncmp(buffer, "/", 1) == 0) {
            char cmd[20], arg[50];
            // Simple parsing for 2-argument commands
            int args_count = sscanf(buffer, "%s %s", cmd, arg);

            if (strcmp(cmd, "/creategroup") == 0) {
                if(args_count < 2) { send(cli->socket, "SERVER: Usage /creategroup [name]\n", 34, 0); continue; }
                int new_id = create_group(arg, cli->name);
                if (new_id != -1) {
                    cli->room_id = new_id;
                    send(cli->socket, "SERVER: Group created. You are Admin.\n", 38, 0);
                } else {
                    send(cli->socket, "SERVER: Group name exists or limit reached.\n", 44, 0);
                }
                continue;
            }
            else if (strcmp(cmd, "/joingroup") == 0) {
                if(args_count < 2) { send(cli->socket, "SERVER: Usage /joingroup [name]\n", 32, 0); continue; }
                int gid = get_group_id_by_name(arg);
                if (gid != -1) {
                    if (join_group(gid, cli->name)) {
                        // Leave old room
                        char leave_msg[100];
                        sprintf(leave_msg, "SERVER:%s left for another channel.", cli->name);
                        send_to_room(leave_msg, cli->room_id, cli->socket);

                        cli->room_id = gid;
                        send_history_to_client(cli->socket, gid);
                        char msg[100]; sprintf(msg, "SERVER: Joined group %s.", arg);
                        send(cli->socket, msg, strlen(msg), 0);
                    } else {
                        send(cli->socket, "SERVER: You are banned or group error.\n", 39, 0);
                    }
                } else {
                    send(cli->socket, "SERVER: Group not found.\n", 24, 0);
                }
                continue;
            }
            // Admin commands for Custom Groups (ID >= 100)
            else if (cli->room_id >= 100) { 
                int is_adm = is_admin(cli->room_id, cli->name);
                
                if (strcmp(cmd, "/kick") == 0 && is_adm) {
                    kick_user(cli->room_id, arg);
                    char msg[100]; sprintf(msg, "SERVER: Kicked %s.", arg);
                    send_to_room(msg, cli->room_id, cli->socket);
                    
                    // Force move the kicked user in memory
                    pthread_mutex_lock(&clients_mutex);
                    for(int i=0; i<MAX_CLIENTS; i++) {
                        if(clients[i] && strcmp(clients[i]->name, arg) == 0 && clients[i]->room_id == cli->room_id) {
                            clients[i]->room_id = 1; // Send to General
                            send(clients[i]->socket, "SERVER: You were kicked from the group.\n", 38, 0);
                        }
                    }
                    pthread_mutex_unlock(&clients_mutex);
                    continue;
                }
                else if (strcmp(cmd, "/ban") == 0 && is_adm) {
                    ban_user(cli->room_id, arg);
                    char msg[100]; sprintf(msg, "SERVER: Banned %s.", arg);
                    send_to_room(msg, cli->room_id, cli->socket);

                    pthread_mutex_lock(&clients_mutex);
                    for(int i=0; i<MAX_CLIENTS; i++) {
                        if(clients[i] && strcmp(clients[i]->name, arg) == 0 && clients[i]->room_id == cli->room_id) {
                            clients[i]->room_id = 1; 
                            send(clients[i]->socket, "SERVER: You were banned from the group.\n", 38, 0);
                        }
                    }
                    pthread_mutex_unlock(&clients_mutex);
                    continue;
                }
                else if (strcmp(cmd, "/deletegroup") == 0 && is_adm) {
                    delete_group(cli->room_id);
                    send(cli->socket, "SERVER: Group deleted.\n", 23, 0);
                    cli->room_id = 1; // Admin goes back to general
                    continue;
                }
            }
        }

        // ======================================================
        // EXISTING COMMANDS & CHAT (Unchanged logic)
        // ======================================================

        // 1. /users (Get List)
        if (strncmp(buffer, "/users", 6) == 0) {
            char user_list[4096] = "USER_LIST:";
            pthread_mutex_lock(&clients_mutex);
            for(int i=0; i<MAX_CLIENTS; i++) {
                if(clients[i] && clients[i]->is_logged_in) { // Only show logged in users
                    strcat(user_list, clients[i]->name);
                    strcat(user_list, ",");
                }
            }
            pthread_mutex_unlock(&clients_mutex);
            send(cli->socket, user_list, strlen(user_list), 0);
        }
        
        // 2. /msg (Private Message)
        else if (strncmp(buffer, "/msg ", 5) == 0) {
            char *target = strtok(buffer + 5, " ");
            char *text = strtok(NULL, ""); 

            if (target && text) {
                pthread_mutex_lock(&clients_mutex);
                int found = 0;
                for(int i=0; i<MAX_CLIENTS; i++) {
                    if(clients[i] && strcmp(clients[i]->name, target) == 0 && clients[i]->is_logged_in) {
                        char out_msg[4096];
                        sprintf(out_msg, "PRIVATE:%s:%s", cli->name, text);
                        send(clients[i]->socket, out_msg, strlen(out_msg), 0);
                        
                        char echo_msg[4096];
                        sprintf(echo_msg, "PRIVATE_SELF:%s:%s", target, text);
                        send(cli->socket, echo_msg, strlen(echo_msg), 0);
                        
                        found = 1;
                        break;
                    }
                }
                pthread_mutex_unlock(&clients_mutex);
                if(!found) {
                    char *err = "SERVER:User not found or not logged in.";
                    send(cli->socket, err, strlen(err), 0);
                }
            }
        }

        // 3. /join (Switch Standard Public Rooms)
        else if (strncmp(buffer, "/join ", 6) == 0) {
            int new_room = atoi(buffer + 6);
            if(new_room < 1) new_room = 1; 

            sprintf(formatted_msg, "SERVER:%s left for another channel.", cli->name);
            send_to_room(formatted_msg, cli->room_id, cli->socket);

            cli->room_id = new_room;
            send_history_to_client(cli->socket, new_room);

            char *room_name = (new_room == 1) ? "General" : (new_room == 2) ? "Study" : (new_room == 3) ? "Gaming" : "Custom Group";
            sprintf(formatted_msg, "SERVER:%s joined %s Channel.", cli->name, room_name);
            send_to_room(formatted_msg, cli->room_id, cli->socket);
        }
        
        // 4. Public Message
        else {
            snprintf(formatted_msg, sizeof(formatted_msg), "%s: %s", cli->name, buffer);
            send_to_room(formatted_msg, cli->room_id, cli->socket);
        }
    }

    remove_client(cli->socket);
    close(cli->socket);
    return NULL;
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(PORT);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 10);

    printf("=== SERVER STARTED: AUTH & GROUPS ENABLED ===\n");
    load_groups(); // NEW: Load groups from file on start

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        
        pthread_mutex_lock(&clients_mutex);
        int added = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i]) {
                Client *cli = (Client *)malloc(sizeof(Client));
                cli->socket = new_socket;
                cli->id = uid_counter++;
                cli->is_admin = (i == 0); 
                cli->room_id = 0; 
                cli->is_logged_in = 0; // NEW: Not logged in by default
                clients[i] = cli;
                
                pthread_t tid;
                pthread_create(&tid, NULL, handle_client, (void *)cli);
                pthread_detach(tid);
                added = 1;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (!added) close(new_socket);
    }
    return 0;
}