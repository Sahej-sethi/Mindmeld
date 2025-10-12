#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 12345
#define MAX_CLIENTS 10
#define BUFFER_SIZE 8192
#define TEXT_MAX_LEN 4096
#define AUTH_PASSWORD "secret123" // The password clients must send to connect

// Global State
char sharedText[TEXT_MAX_LEN] = "Welcome to the collaborative notepad!";
int sharedTextLength = 0;
pthread_mutex_t text_mutex = PTHREAD_MUTEX_INITIALIZER;

// Client Information Structure
typedef struct {
    int socket;
    int id; // Unique ID (used as color index on client side)
    int cursor;
    int active;
    int is_authenticated; // <-- ADDED: Authentication state flag
    pthread_t thread;
} Client;

Client clients[MAX_CLIENTS];
int next_client_id = 1; // Start IDs from 1

// =================================================================================
// Operational Transformation (OT) Logic
// Applies the local edit and transforms all other client cursors
// =================================================================================
/**
 * @brief Transforms a cursor position based on a performed operation (I or D).
 * * @param op_type 'I' (Insert) or 'D' (Delete)
 * @param op_pos The position where the operation occurred.
 * @param old_cursor The cursor position of another client *before* the operation.
 * @return int The new, transformed cursor position.
 */
int transform_cursor(char op_type, int op_pos, int old_cursor) {
    if (op_type == 'I') {
        // If cursor is at or after the insertion point, shift it right by 1
        if (old_cursor >= op_pos) {
            return old_cursor + 1;
        }
    } else if (op_type == 'D') {
        // If cursor is strictly after the deletion point, shift it left by 1.
        if (old_cursor > op_pos) {
            return old_cursor - 1;
        }
    }
    return old_cursor;
}

// =================================================================================
// Broadcast Function
// =================================================================================
/**
 * @brief Sends the full current state (text, cursors, colors) to all active clients.
 * * NOTE: This function ASSUMES the 'text_mutex' is already locked by the calling thread.
 * * @param exclude_socket Socket to exclude from the broadcast (usually the sender).
 * @param send_to_self If true, sends to the exclude_socket as well (used for initial state/OT confirmation).
 */
void broadcast_state(int exclude_socket, int send_to_self) {
    // DEADLOCK FIX: Removed pthread_mutex_lock(&text_mutex); 
    // The calling function (handle_client) ensures the lock is held.
    
    // 1. Build the cursors and colors strings
    char cursors_str[512] = "";
    char colors_str[512] = "";
    
    // Only count authenticated clients for cursors/colors
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].is_authenticated) { // <-- MODIFIED: Check is_authenticated
            char temp_cursor[16], temp_color[16];
            snprintf(temp_cursor, sizeof(temp_cursor), "%d|", clients[i].cursor);
            strcat(cursors_str, temp_cursor);
            snprintf(temp_color, sizeof(temp_color), "%d|", clients[i].id);
            strcat(colors_str, temp_color);
        }
    }
    
    // Trim trailing '|'
    if (strlen(cursors_str) > 0) cursors_str[strlen(cursors_str) - 1] = '\0';
    if (strlen(colors_str) > 0) colors_str[strlen(colors_str) - 1] = '\0';
    
    // 2. Build the final broadcast message
    char full_message[BUFFER_SIZE * 2];
    // This message format is parsed by the client to get the text, cursors, and colors
    snprintf(full_message, sizeof(full_message), 
             "%s|CURSORS%s|COLORS%s", 
             sharedText, cursors_str, colors_str);
             
    // 3. Send the message
    for (int i = 0; i < MAX_CLIENTS; i++) {
        // <-- MODIFIED: Only send state to clients who are active AND authenticated.
        // Special case: if send_to_self is true, we send to all active clients (for initial auth-success state or OT echo)
        // If send_to_self is false, we only send to authenticated clients other than the excuded socket.
        if (clients[i].active && clients[i].is_authenticated) { 
            if (clients[i].socket != exclude_socket || send_to_self) {
                send(clients[i].socket, full_message, strlen(full_message), 0);
            }
        } else if (clients[i].active && send_to_self) {
             // Edge case for initial broadcast_state(-1, 1) when a client first connects/auths successfully
             // The only time send_to_self is true is when a client just authenticated or after a client's edit.
             // We ensure the newly authenticated client gets the state even if this logic is complex.
             // The main initial broadcast happens *after* 'AUTH OK' so the flag will be set.
             if (clients[i].socket == exclude_socket) { // This is mainly for the immediate post-auth send
                 send(clients[i].socket, full_message, strlen(full_message), 0);
             }
        }
    }
    // DEADLOCK FIX: Removed pthread_mutex_unlock(&text_mutex);
}

// =================================================================================
// Client Handling Thread
// =================================================================================
void *handle_client(void *arg) {
    Client *client = (Client *)arg;
    char buffer[1024];
    int bytes_read;
    char *saveptr; // Pointer for strtok_r to ensure thread-safety
    
    printf("Client %d connecting (Socket %d).\n", client->id, client->socket);

    // --- AUTHENTICATION BLOCK (Initial Communication) ---
    // The very first message must be the password.
    bytes_read = recv(client->socket, buffer, 1023, 0);
    
    if (bytes_read <= 0) {
        printf("Client attempted to connect (Socket %d), but disconnected during auth.\n", client->socket);
        goto cleanup;
    }
    
    buffer[bytes_read] = '\0';
    
    if (strcmp(buffer, AUTH_PASSWORD) != 0) {
        printf("Client attempted to connect (Socket %d) with wrong password: '%s'. Rejected.\n", client->socket, buffer);
        send(client->socket, "AUTH FAIL", 9, 0);
        goto cleanup;
    }
    
    // Authentication successful
    send(client->socket, "AUTH OK", 7, 0);
    printf("Client %d authenticated (Socket %d). Starting communication.\n", client->id, client->socket);
    
    pthread_mutex_lock(&text_mutex);
    client->is_authenticated = 1; // <-- MODIFIED: Set the flag on success
    
    // 1. Initial greeting (send ID)
    char init_msg[32];
    snprintf(init_msg, sizeof(init_msg), "INIT|%d", client->id);
    send(client->socket, init_msg, strlen(init_msg), 0);
    
    // 2. Send initial full state (send_to_self=1 ensures the new client gets it)
    broadcast_state(client->socket, 1);
    pthread_mutex_unlock(&text_mutex);
    // --- END AUTH BLOCK ---
    
    // 3. Main communication loop
    while ((bytes_read = recv(client->socket, buffer, 1023, 0)) > 0) {
        buffer[bytes_read] = '\0';
        
        // --- SECURITY CHECK ---
        // While technically redundant because the loop only runs after auth success,
        // it's a good practice to ensure state for document ops.
        if (!client->is_authenticated) { 
             // Should not happen if protocol is followed, but handles out-of-order state.
             printf("Client %d sent data before being authenticated. Ignoring.\n", client->id);
             continue;
        }
        // Expected format: ACTION|POS|CHAR (e.g., I|5|A, D|4, C|10)
        
        // Using strtok_r for thread-safe tokenization
        char *token = strtok_r(buffer, "|", &saveptr);
        if (!token) continue;
        
        char action = token[0];
        
        // Get client's index in the clients array for quick access
        int client_idx = -1;
        for(int i=0; i<MAX_CLIENTS; i++) {
            if (clients[i].id == client->id) {
                client_idx = i;
                break;
            }
        }
        if (client_idx == -1) continue;
        
        pthread_mutex_lock(&text_mutex); // Lock for shared state modification/access
        
        if (action == 'C') { // Cursor Update
            int new_pos = atoi(strtok_r(NULL, "|", &saveptr));
            // Safety check
            if (new_pos >= 0 && new_pos <= sharedTextLength) {
                clients[client_idx].cursor = new_pos;
            }
            printf("Client %d updated cursor to %d.\n", client->id, clients[client_idx].cursor);
            // Broadcast state to sync other clients' cursors immediately
            broadcast_state(client->socket, 0); // exclude sender
        
        } else if (action == 'I') { // Insert
            int pos = atoi(strtok_r(NULL, "|", &saveptr));
            char *char_str = strtok_r(NULL, "|", &saveptr);
            char ch = (char_str && char_str[0] != '\0') ? char_str[0] : '\n';
            
            // Apply Insert
            if (pos >= 0 && pos <= sharedTextLength && sharedTextLength < TEXT_MAX_LEN - 1) {
                for (int i = sharedTextLength; i >= pos; i--) {
                    sharedText[i + 1] = sharedText[i];
                }
                sharedText[pos] = ch;
                sharedTextLength++;
                sharedText[sharedTextLength] = '\0';
                
                // OT: Transform all client cursors
                clients[client_idx].cursor = pos + 1; // Sender's cursor moves past the new char
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].active && clients[i].id != client->id) {
                        clients[i].cursor = transform_cursor('I', pos, clients[i].cursor);
                    }
                }
                printf("Client %d inserted '%c' at %d. Length: %d\n", client->id, ch == '\n' ? 'N' : ch, pos, sharedTextLength);
            }
            // Broadcast the new state and the sender's transformed cursor (send_to_self = 1)
            broadcast_state(-1, 1); // Send to all authenticated clients
            
        } else if (action == 'D') { // Delete
            int pos = atoi(strtok_r(NULL, "|", &saveptr));
            
            // Apply Delete (pos is the index of the character that was before the cursor)
            if (pos >= 0 && pos < sharedTextLength) {
                for (int i = pos; i < sharedTextLength; i++) {
                    sharedText[i] = sharedText[i + 1];
                }
                sharedTextLength--;
                sharedText[sharedTextLength] = '\0';
                // OT: Transform all client cursors
                clients[client_idx].cursor = pos; // Sender's cursor stays at the deletion point
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].active && clients[i].id != client->id) {
                        clients[i].cursor = transform_cursor('D', pos, clients[i].cursor);
                    }
                }
                printf("Client %d deleted at %d. Length: %d\n", client->id, pos, sharedTextLength);
            }
            // Broadcast the new state and the sender's transformed cursor (send_to_self = 1)
            broadcast_state(-1, 1); // Send to all authenticated clients
        }
        
        pthread_mutex_unlock(&text_mutex); // Unlock after all modifications/broadcasts are complete
    }
    
    // Client disconnected
    printf("Client %d disconnected (Socket %d).\n", client->id, client->socket);

cleanup:
    // Mark client as inactive and broadcast state change
    pthread_mutex_lock(&text_mutex);
    client->active = 0;
    client->is_authenticated = 0; // <-- MODIFIED: Reset auth flag
    // Only broadcast if the client was authenticated to begin with, otherwise no state change to report
    if (client->is_authenticated) {
        broadcast_state(-1, 1); // Notify all *authenticated* clients one last time
    }
    pthread_mutex_unlock(&text_mutex);
    
    close(client->socket);
    return NULL;
}

// =================================================================================
// Main Server Function
// =================================================================================
int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;
    
    // Initialize shared text length
    sharedTextLength = strlen(sharedText);
    
    // Initialize client states
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].active = 0;
        clients[i].is_authenticated = 0; // <-- MODIFIED: Initialize auth flag
    }
    
    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }
    // Forcefully attaching socket to the port 12345
    // We keep SO_REUSEADDR to allow rapid server restarts.
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    // Forcefully attaching socket to the port 12345
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server started. Listening on port %d...\n", PORT);
    printf("Initial Text: \"%s\"\n", sharedText);
    
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }
        
        // Find an available slot
        int client_index = -1;
        pthread_mutex_lock(&text_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active == 0) {
                client_index = i;
                break;
            }
        }
        pthread_mutex_unlock(&text_mutex);
        
        if (client_index != -1) {
            clients[client_index].socket = new_socket;
            clients[client_index].id = next_client_id++;
            clients[client_index].cursor = sharedTextLength; // Initial cursor at end of text
            clients[client_index].active = 1;
            clients[client_index].is_authenticated = 0; // <-- MODIFIED: Explicitly unauthenticated initially
            
            // Create a thread for the new client
            if (pthread_create(&clients[client_index].thread, NULL, handle_client, &clients[client_index]) != 0) {
                perror("Thread creation failed");
                clients[client_index].active = 0;
                close(new_socket);
            }
        } else {
            // Max clients reached
            const char *max_msg = "Server is full.";
            send(new_socket, max_msg, strlen(max_msg), 0);
            close(new_socket);
            printf("Connection rejected: server full.\n");
        }
    }
    
    // Clean up (unreachable in this infinite loop server)
    close(server_fd);
    return 0;
}