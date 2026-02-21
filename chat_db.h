#ifndef CHAT_DB_H
#define CHAT_DB_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GROUPS 100
#define DB_USERS "users.txt"
#define DB_GROUPS "groups.txt"

typedef struct {
    int id;
    char name[50];
    char admins[1024];  // Comma separated usernames
    char members[2048]; // Comma separated usernames
    char banned[1024];  // Comma separated usernames
} Group;

// Global group storage
extern Group groups[MAX_GROUPS];
extern int group_count;

// Auth Functions
int register_user(const char *username, const char *password);
int login_user(const char *username, const char *password);

// Group Functions
void load_groups();
void save_groups();
int create_group(const char *name, const char *creator);
int join_group(int group_id, const char *username); // Returns: 1=Success, 0=Banned/Fail
int is_admin(int group_id, const char *username);
void kick_user(int group_id, const char *username);
void ban_user(int group_id, const char *username);
void make_admin(int group_id, const char *username);
int get_group_id_by_name(const char *name);
int delete_group(int group_id);

// Helper to check if a user string is in a comma-separated list
int is_in_list(const char *list, const char *name);

#endif