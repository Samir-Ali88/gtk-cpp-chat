#include "chat_db.h"

Group groups[MAX_GROUPS];
int group_count = 0;

// --- AUTHENTICATION ---

int register_user(const char *username, const char *password) {
    FILE *f = fopen(DB_USERS, "a+");
    if (!f) return 0;

    char u[50], p[50];
    rewind(f);
    while (fscanf(f, "%s %s", u, p) != EOF) {
        if (strcmp(u, username) == 0) {
            fclose(f);
            return 0; // User exists
        }
    }

    fprintf(f, "%s %s\n", username, password);
    fclose(f);
    return 1;
}

int login_user(const char *username, const char *password) {
    FILE *f = fopen(DB_USERS, "r");
    if (!f) return 0;

    char u[50], p[50];
    while (fscanf(f, "%s %s", u, p) != EOF) {
        if (strcmp(u, username) == 0 && strcmp(p, password) == 0) {
            fclose(f);
            return 1; // Success
        }
    }
    fclose(f);
    return 0;
}

// --- GROUP UTILS ---

int is_in_list(const char *list, const char *name) {
    char temp[2048];
    strcpy(temp, list);
    char *token = strtok(temp, ",");
    while (token) {
        if (strcmp(token, name) == 0) return 1;
        token = strtok(NULL, ",");
    }
    return 0;
}

void add_to_list(char *list, const char *name) {
    if (is_in_list(list, name)) return;
    if (strlen(list) > 0) strcat(list, ",");
    strcat(list, name);
}

void remove_from_list(char *list, const char *name) {
    char temp[2048] = "";
    char copy[2048];
    strcpy(copy, list);
    char *token = strtok(copy, ",");
    while (token) {
        if (strcmp(token, name) != 0) {
            if (strlen(temp) > 0) strcat(temp, ",");
            strcat(temp, token);
        }
        token = strtok(NULL, ",");
    }
    strcpy(list, temp);
}

// --- GROUP MANAGEMENT ---

void save_groups() {
    FILE *f = fopen(DB_GROUPS, "w");
    if (!f) return;
    for (int i = 0; i < group_count; i++) {
        // Format: ID|Name|Admins|Members|Banned
        fprintf(f, "%d|%s|%s|%s|%s\n", groups[i].id, groups[i].name, 
                groups[i].admins, groups[i].members, groups[i].banned);
    }
    fclose(f);
}

void load_groups() {
    FILE *f = fopen(DB_GROUPS, "r");
    if (!f) return;
    
    group_count = 0;
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        
        Group *g = &groups[group_count];
        char *ptr = line;

        // Parse using simple pipe delimiters
        sscanf(strtok_r(ptr, "|", &ptr), "%d", &g->id);
        strcpy(g->name, strtok_r(NULL, "|", &ptr));
        strcpy(g->admins, strtok_r(NULL, "|", &ptr));
        strcpy(g->members, strtok_r(NULL, "|", &ptr));
        strcpy(g->banned, strtok_r(NULL, "|", &ptr));
        
        // Handle empty fields turning into (null) from strtok logic
        if (!g->admins[0]) strcpy(g->admins, "");
        if (!g->members[0]) strcpy(g->members, "");
        if (!g->banned[0]) strcpy(g->banned, "");

        group_count++;
    }
    fclose(f);
}

int create_group(const char *name, const char *creator) {
    if (group_count >= MAX_GROUPS) return -1;
    
    // Check duplication
    for(int i=0; i<group_count; i++) {
        if(strcmp(groups[i].name, name) == 0) return -1;
    }

    Group *g = &groups[group_count];
    g->id = 100 + group_count; // Custom groups start at 100
    strcpy(g->name, name);
    strcpy(g->admins, creator);
    strcpy(g->members, creator);
    strcpy(g->banned, "");
    
    group_count++;
    save_groups();
    return g->id;
}

int join_group(int group_id, const char *username) {
    for (int i = 0; i < group_count; i++) {
        if (groups[i].id == group_id) {
            if (is_in_list(groups[i].banned, username)) return 0; // Banned
            add_to_list(groups[i].members, username);
            save_groups();
            return 1;
        }
    }
    return 0; // Not found
}

int is_admin(int group_id, const char *username) {
    for (int i = 0; i < group_count; i++) {
        if (groups[i].id == group_id) {
            return is_in_list(groups[i].admins, username);
        }
    }
    return 0;
}

void kick_user(int group_id, const char *username) {
    for (int i = 0; i < group_count; i++) {
        if (groups[i].id == group_id) {
            remove_from_list(groups[i].members, username);
            remove_from_list(groups[i].admins, username); // Also remove admin role
            save_groups();
            return;
        }
    }
}

void ban_user(int group_id, const char *username) {
    for (int i = 0; i < group_count; i++) {
        if (groups[i].id == group_id) {
            kick_user(group_id, username);
            add_to_list(groups[i].banned, username);
            save_groups();
            return;
        }
    }
}

void make_admin(int group_id, const char *username) {
    for (int i = 0; i < group_count; i++) {
        if (groups[i].id == group_id) {
            add_to_list(groups[i].admins, username);
            save_groups();
        }
    }
}

int get_group_id_by_name(const char *name) {
    for (int i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) return groups[i].id;
    }
    return -1;
}

int delete_group(int group_id) {
    for (int i = 0; i < group_count; i++) {
        if (groups[i].id == group_id) {
            // Simple deletion: swap with last
            groups[i] = groups[group_count - 1];
            group_count--;
            save_groups();
            return 1;
        }
    }
    return 0;
}