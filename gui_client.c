// client.c - Notification Dot & Persistent History
// Compile: gcc gui_client.c -o client $(pkg-config --cflags --libs gtk+-3.0) -pthread
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 4096

int sock_fd = 0;
char username[50];
int current_mode = 0; // 0 = Public, 1 = Private
char private_target[50] = "";

GtkWidget *message_list_box, *scrolled_window, *entry_msg, *status_label, *title_label, *send_btn, *alert_badge;

typedef struct { char *text; int type; char *sender; } MsgData; // 0=Mine,1=Others,2=Channel,3=Server,4=Private
typedef struct { char *contact_name; GList *messages; } ChatSession;
GList *private_sessions = NULL;

static void load_css() {
    GtkCssProvider *p = gtk_css_provider_new();
    const char *css = "window,window:backdrop{background-color:#17212b;}.header-box{background-color:#2b5278;padding:10px;}"
        "menu{background-color:#2b5278;color:#fff;border:1px solid #182533;}menuitem{color:#fff;padding:5px;}menuitem:hover{background-color:#3b6288;}"
        "button.hamburger{background:transparent;border:none;color:white;box-shadow:none;padding:0px;}button.hamburger:hover{background:#3b6288;}"
        ".alert-dot{color:#ff5555;font-size:14px;text-shadow:0 0 2px black;}.status-connecting{color:#fec031;font-size:12px;}.status-online{color:#9dff00;font-size:12px;}"
        ".bubble{padding:12px 18px;border-radius:12px;margin:5px;color:white;font-size:16px;}.mine{background-color:#2b5278;margin-left:60px;border-bottom-right-radius:0;}"
        ".others{background-color:#182533;margin-right:60px;border-bottom-left-radius:0;}.channel{background-color:#242f3d;color:#4fa3d1;font-weight:bold;padding:8px;margin:10px 0;border:2px solid #2b5278;border-radius:20px;}"
        ".server{color:#888;font-size:12px;margin:5px;font-style:italic;}"
        
        /* Private Chat Colors */
        ".private.mine{background-color:#2E675D;color:white;border:none;}"
        ".private.others{background-color:#24303F;color:#DDE3EA;border:none;}"

        /* Input Area & Cursor Visibility Change Below */
        ".input-area{background-color:#17212b;padding:15px;}"
        "entry{background:#242f3d;color:white;caret-color:#ffffff;border:none;border-radius:20px;padding:15px;font-size:16px;}" 
        
        "button{background:#2b5278;color:white;border-radius:50%;padding:15px;border:none;font-weight:bold;}";
    
    gtk_css_provider_load_from_data(p, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

ChatSession* get_session(const char *c) {
    for (GList *l = private_sessions; l; l = l->next) if (strcmp(((ChatSession*)l->data)->contact_name, c) == 0) return (ChatSession*)l->data;
    ChatSession *s = g_malloc(sizeof(ChatSession)); s->contact_name = g_strdup(c); s->messages = NULL;
    private_sessions = g_list_append(private_sessions, s); return s;
}

void add_to_history(const char *c, MsgData *m) {
    ChatSession *s = get_session(c); MsgData *cp = g_malloc(sizeof(MsgData));
    cp->text = g_strdup(m->text); cp->sender = g_strdup(m->sender); cp->type = m->type;
    s->messages = g_list_append(s->messages, cp);
}

static void scroll_to_bottom() { 
    GtkAdjustment *a = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled_window)); 
    gtk_adjustment_set_value(a, gtk_adjustment_get_upper(a)); 
}
void clear_chat_window() {
    GList *c = gtk_container_get_children(GTK_CONTAINER(message_list_box));
    for(GList *i = c; i; i = i->next) gtk_widget_destroy(GTK_WIDGET(i->data));
    g_list_free(c);
}

static gboolean append_message(gpointer user_data) {
    MsgData *d = (MsgData *)user_data; if(!d) return FALSE;
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0), *b = gtk_label_new(d->text);
    gtk_label_set_line_wrap(GTK_LABEL(b), TRUE); gtk_label_set_max_width_chars(GTK_LABEL(b), 60);
    GtkStyleContext *sc = gtk_widget_get_style_context(b); gtk_style_context_add_class(sc, "bubble");
    
    if (d->type == 0) { // Mine
        gtk_widget_set_halign(row, GTK_ALIGN_END); gtk_label_set_xalign(GTK_LABEL(b), 0.0);
        gtk_style_context_add_class(sc, "mine"); gtk_box_pack_end(GTK_BOX(row), b, 0, 0, 0);
    } else if (d->type == 2 || d->type == 3) { // Channel/Server
        gtk_widget_set_halign(row, GTK_ALIGN_CENTER); gtk_style_context_add_class(sc, d->type == 2 ? "channel" : "server");
        gtk_box_pack_start(GTK_BOX(row), b, 1, 1, 0);
    } else { // Private or Others
        if (d->type == 4) gtk_style_context_add_class(sc, "private");
        if (d->type == 4 && strcmp(d->sender, "Me") == 0) {
             gtk_widget_set_halign(row, GTK_ALIGN_END); gtk_style_context_add_class(sc, "mine"); gtk_box_pack_end(GTK_BOX(row), b, 0, 0, 0);
        } else {
             gtk_widget_set_halign(row, GTK_ALIGN_START); gtk_style_context_add_class(sc, "others"); gtk_box_pack_start(GTK_BOX(row), b, 0, 0, 0);
        }
    }
    gtk_container_add(GTK_CONTAINER(message_list_box), row); gtk_widget_show_all(row);
    g_timeout_add(100, (GSourceFunc)scroll_to_bottom, NULL);
    g_free(d->text); g_free(d->sender); g_free(d); return FALSE;
}

void reload_history_for_ui(const char *c) {
    for (GList *l = get_session(c)->messages; l; l = l->next) {
        MsgData *s = (MsgData*)l->data, *t = g_malloc(sizeof(MsgData));
        t->text = g_strdup(s->text); t->sender = g_strdup(s->sender); t->type = s->type; append_message(t);
    }
}

static gboolean show_alert_dot(gpointer d) { gtk_widget_set_visible(alert_badge, TRUE); return FALSE; }
extern gboolean build_user_list_dialog(gpointer); 

void *receive_handler(void *arg) {
    char buf[BUFFER_SIZE]; int n;
    while ((n = recv(sock_fd, buf, BUFFER_SIZE - 1, 0)) > 0) {
        buf[n] = '\0';
        if (strncmp(buf, "USER_LIST:", 10) == 0) { g_idle_add(build_user_list_dialog, g_strdup(buf+10)); continue; }
        
        MsgData *m = g_malloc(sizeof(MsgData)); m->sender = g_strdup("Unknown"); int disp = 0;
        if (strncmp(buf, "PRIVATE:", 8) == 0) {
            m->type = 4; char *p = buf + 8, *s = strtok_r(p, ":", &p);
            if(s) { g_free(m->sender); m->sender = g_strdup(s); } m->text = g_strdup(p?p:"");
            add_to_history(m->sender, m);
            if (current_mode == 1 && strcmp(private_target, m->sender) == 0) disp = 1; else g_idle_add(show_alert_dot, NULL);
        } else if (strncmp(buf, "PRIVATE_SELF:", 13) == 0) {
            m->type = 4; char *p = buf + 13, *t = strtok_r(p, ":", &p);
            g_free(m->sender); m->sender = g_strdup("Me"); m->text = g_strdup(p?p:"");
            if(t) add_to_history(t, m);
            if (current_mode == 1 && t && strcmp(private_target, t) == 0) disp = 1;
        } else if (strncmp(buf, "CHANNEL:", 8) == 0) { m->type = 2; m->text = g_strdup(buf+8); if(!current_mode) disp = 1; }
        else if (strncmp(buf, "SERVER:", 7) == 0) { m->type = 3; m->text = g_strdup(buf+7); if(!current_mode) disp = 1; }
        else { m->type = 1; m->text = g_strdup(buf); if(!current_mode) disp = 1; }

        if(disp) g_idle_add(append_message, m); else { g_free(m->text); g_free(m->sender); g_free(m); }
    }
    return NULL;
}

void send_message() {
    const char *t = gtk_entry_get_text(GTK_ENTRY(entry_msg)); if (!strlen(t)) return;
    if (current_mode == 1) {
        char cmd[BUFFER_SIZE]; snprintf(cmd, BUFFER_SIZE, "/msg %s %s", private_target, t); send(sock_fd, cmd, strlen(cmd), 0);
    } else {
        if (send(sock_fd, t, strlen(t), 0) < 0) return;
        if (strncmp(t, "/", 1) != 0) {
            MsgData *m = g_malloc(sizeof(MsgData)); m->type = 0; m->text = g_strdup(t); m->sender = g_strdup("Me"); append_message(m);
        }
    }
    gtk_entry_set_text(GTK_ENTRY(entry_msg), "");
}

void on_user_selected(GtkWidget *w, gpointer d) {
    strcpy(private_target, gtk_button_get_label(GTK_BUTTON(w))); current_mode = 1;
    char t[100]; sprintf(t, "Private: %s", private_target); gtk_label_set_text(GTK_LABEL(title_label), t);
    gtk_widget_set_visible(alert_badge, FALSE); clear_chat_window(); reload_history_for_ui(private_target); gtk_widget_destroy(GTK_WIDGET(d));
}

gboolean build_user_list_dialog(gpointer ul) {
    char *list = (char *)ul, *tok = strtok(list, ",");
    GtkWidget *d = gtk_window_new(GTK_WINDOW_TOPLEVEL), *scr = gtk_scrolled_window_new(NULL,NULL), *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_window_set_title(GTK_WINDOW(d), "Online Users"); gtk_window_set_default_size(GTK_WINDOW(d), 250, 300);
    gtk_window_set_transient_for(GTK_WINDOW(d), GTK_WINDOW(gtk_widget_get_toplevel(message_list_box)));
    gtk_container_add(GTK_CONTAINER(d), scr); gtk_container_add(GTK_CONTAINER(scr), vb);
    while (tok) {
        if (strcmp(tok, username) && strlen(tok)) {
            GtkWidget *b = gtk_button_new_with_label(tok); g_signal_connect(b, "clicked", G_CALLBACK(on_user_selected), d);
            gtk_box_pack_start(GTK_BOX(vb), b, 0, 0, 5);
        } tok = strtok(NULL, ",");
    }
    gtk_widget_show_all(d); g_free(list); return FALSE;
}

void on_join_group(GtkWidget *w, gpointer d) {
    int id = GPOINTER_TO_INT(d); current_mode = 0; char cmd[20]; sprintf(cmd, "/join %d", id); send(sock_fd, cmd, strlen(cmd), 0);
    char t[50]; sprintf(t, "%s Channel", (id == 1) ? "General" : (id == 2) ? "Study" : "Gaming");
    gtk_label_set_text(GTK_LABEL(title_label), t); clear_chat_window();
}
void on_request_private_chat(GtkWidget *w, gpointer d) { send(sock_fd, "/users", 6, 0); }
void on_hamburger_clicked(GtkButton *b, gpointer d) { gtk_menu_popup_at_widget(GTK_MENU(d), GTK_WIDGET(b), GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL); gtk_widget_set_visible(alert_badge, FALSE); }

GtkWidget* create_menu() {
    GtkWidget *m = gtk_menu_new(), *i1 = gtk_menu_item_new_with_label("General"), *i2 = gtk_menu_item_new_with_label("Study"), *i3 = gtk_menu_item_new_with_label("Gaming");
    g_signal_connect(i1, "activate", G_CALLBACK(on_join_group), GINT_TO_POINTER(1)); gtk_menu_shell_append(GTK_MENU_SHELL(m), i1);
    g_signal_connect(i2, "activate", G_CALLBACK(on_join_group), GINT_TO_POINTER(2)); gtk_menu_shell_append(GTK_MENU_SHELL(m), i2);
    g_signal_connect(i3, "activate", G_CALLBACK(on_join_group), GINT_TO_POINTER(3)); gtk_menu_shell_append(GTK_MENU_SHELL(m), i3);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    GtkWidget *ip = gtk_menu_item_new_with_label("Private Messages..."); g_signal_connect(ip, "activate", G_CALLBACK(on_request_private_chat), NULL); gtk_menu_shell_append(GTK_MENU_SHELL(m), ip);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    GtkWidget *ie = gtk_menu_item_new_with_label("Exit"); g_signal_connect(ie, "activate", G_CALLBACK(gtk_main_quit), NULL); gtk_menu_shell_append(GTK_MENU_SHELL(m), ie);
    gtk_widget_show_all(m); return m;
}

int show_login_dialog(char *ip, char *user) {
    GtkWidget *d = gtk_dialog_new_with_buttons("Login", NULL, GTK_DIALOG_MODAL, "Connect", 1, "Cancel", 0, NULL);
    GtkWidget *g = gtk_grid_new(), *ie = gtk_entry_new(), *ue = gtk_entry_new();
    gtk_window_set_default_size(GTK_WINDOW(d), 300, 200); gtk_grid_set_row_spacing(GTK_GRID(g), 15); gtk_grid_set_column_spacing(GTK_GRID(g), 10);
    gtk_entry_set_text(GTK_ENTRY(ie), "127.0.0.1"); gtk_entry_set_placeholder_text(GTK_ENTRY(ue), "Username");
    gtk_grid_attach(GTK_GRID(g), gtk_label_new("IP:"), 0, 0, 1, 1); gtk_grid_attach(GTK_GRID(g), ie, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), gtk_label_new("Name:"), 0, 1, 1, 1); gtk_grid_attach(GTK_GRID(g), ue, 1, 1, 1, 1);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), g); gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    if (r == 1) { strcpy(ip, gtk_entry_get_text(GTK_ENTRY(ie))); strcpy(user, gtk_entry_get_text(GTK_ENTRY(ue))); }
    gtk_widget_destroy(d); return r == 1;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv); load_css();
    char server_ip[50]; if (!show_login_dialog(server_ip, username)) return 0;

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL), *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
gtk_window_set_default_size(GTK_WINDOW(win), 1000, 800);
gtk_window_set_title(GTK_WINDOW(win), username); // <--- ADD THIS LINE
    // REMOVED: gtk_window_set_title(GTK_WINDOW(win), "Chat System");
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL); gtk_container_add(GTK_CONTAINER(win), vbox);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5), *ovl = gtk_overlay_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(top), "header-box");
    
    GtkWidget *ham = gtk_button_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU);
    gtk_style_context_add_class(gtk_widget_get_style_context(ham), "hamburger");
    g_signal_connect(ham, "clicked", G_CALLBACK(on_hamburger_clicked), create_menu());
    
    alert_badge = gtk_label_new("●"); gtk_style_context_add_class(gtk_widget_get_style_context(alert_badge), "alert-dot");
    gtk_widget_set_halign(alert_badge, GTK_ALIGN_END); gtk_widget_set_valign(alert_badge, GTK_ALIGN_START);
    gtk_widget_set_visible(alert_badge, FALSE); 
    gtk_container_add(GTK_CONTAINER(ovl), ham); gtk_overlay_add_overlay(GTK_OVERLAY(ovl), alert_badge);
    gtk_box_pack_start(GTK_BOX(top), ovl, 0, 0, 0);

    GtkWidget *tbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    title_label = gtk_label_new("General Channel"); status_label = gtk_label_new("Connecting...");
    PangoAttrList *att = pango_attr_list_new(); pango_attr_list_insert(att, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(att, pango_attr_foreground_new(65535, 65535, 65535)); pango_attr_list_insert(att, pango_attr_scale_new(1.2));
    gtk_label_set_attributes(GTK_LABEL(title_label), att); pango_attr_list_unref(att);
    gtk_style_context_add_class(gtk_widget_get_style_context(status_label), "status-connecting");
    gtk_box_pack_start(GTK_BOX(tbox), title_label, 0, 0, 0); gtk_box_pack_start(GTK_BOX(tbox), status_label, 0, 0, 0);
    gtk_box_pack_start(GTK_BOX(top), tbox, 1, 0, 0); gtk_box_pack_start(GTK_BOX(vbox), top, 0, 0, 0);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL); message_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(scrolled_window), message_list_box); gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, 1, 1, 0);

    GtkWidget *inp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); gtk_style_context_add_class(gtk_widget_get_style_context(inp), "input-area");
    entry_msg = gtk_entry_new(); send_btn = gtk_button_new_with_label(" ➤ ");
    g_signal_connect(entry_msg, "activate", G_CALLBACK(send_message), NULL); g_signal_connect(send_btn, "clicked", G_CALLBACK(send_message), NULL);
    gtk_widget_set_sensitive(entry_msg, 0); gtk_widget_set_sensitive(send_btn, 0);
    gtk_box_pack_start(GTK_BOX(inp), entry_msg, 1, 1, 0); gtk_box_pack_start(GTK_BOX(inp), send_btn, 0, 0, 0);
    gtk_box_pack_start(GTK_BOX(vbox), inp, 0, 0, 0);
    
    gtk_widget_show_all(win); gtk_widget_set_visible(alert_badge, FALSE);
    while (gtk_events_pending()) gtk_main_iteration();

    sock_fd = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in sa; sa.sin_family = AF_INET; sa.sin_port = htons(PORT);
    if (inet_pton(AF_INET, server_ip, &sa.sin_addr) <= 0 || connect(sock_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        gtk_label_set_text(GTK_LABEL(status_label), inet_pton(AF_INET,server_ip,&sa.sin_addr)<=0 ? "Error: Invalid IP" : "Connection Failed.");
    } else {
        gtk_label_set_text(GTK_LABEL(status_label), "● Online"); gtk_style_context_remove_class(gtk_widget_get_style_context(status_label), "status-connecting");
        gtk_style_context_add_class(gtk_widget_get_style_context(status_label), "status-online");
        gtk_widget_set_sensitive(entry_msg, 1); gtk_widget_set_sensitive(send_btn, 1); gtk_widget_grab_focus(entry_msg);
        send(sock_fd, username, strlen(username), 0);
        pthread_t t; pthread_create(&t, NULL, receive_handler, NULL);
    }
    gtk_main(); if (sock_fd) close(sock_fd); return 0;
}