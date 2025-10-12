#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "raylib.h"

#define SERVER_IP "10.2.139.58" // Change to your server IP
#define PORT 12345
#define BUFFER_SIZE 8192
#define MAX_CLIENTS 10
#define LINE_HEIGHT 32
#define TEXT_START_Y 100
#define TEXT_START_X 50
#define SPACE_WIDTH 12 // Consistent width for space character

// --- New Scroll Constants ---
#define SCROLLBAR_WIDTH 15
#define SCROLL_SPEED_V 3 * LINE_HEIGHT // Vertical scroll speed (3 lines)
#define SCROLL_SPEED_H 50              // Horizontal scroll speed (50 pixels)
// ----------------------------

int sock;
char sharedText[BUFFER_SIZE] = {0};
int cursorPos = 0;
int myColorIndex = -1; // Unique ID for this client, received from server
pthread_mutex_t lock;

int otherCursors[MAX_CLIENTS] = {0};
int otherColors[MAX_CLIENTS] = {0};
int otherCount = 0;

// --- Selection State Variables ---
int selectionStart = 0; // Index where mouse press started or where selection was started by keyboard
int selectionEnd = 0;   // Index where mouse is currently (the active cursor position)
bool isSelecting = false;
// ---------------------------------

// --- New Scroll Variables ---
float scrollOffsetY = 0.0f; // Vertical scroll offset (pixels)
float scrollOffsetX = 0.0f; // Horizontal scroll offset (pixels)
// --------------------------

// =================================================================================
// Struct to hold calculated cursor/text position information
// =================================================================================
typedef struct {
    int row;
    int col;
    int x;
    int y;
    int lineCount;
    int maxLineWidth; // Maximum width of any line in pixels
} TextMetrics;

// =================================================================================
// Helper function to calculate the x/y position, row/col, and total metrics for an index
// =================================================================================
TextMetrics getTextMetrics(int targetIndex, const char* text) {
    TextMetrics metrics = {0};
    int currentX = TEXT_START_X;
    int currentY = TEXT_START_Y;
    int currentLineWidth = 0;

    for (int i = 0; i < strlen(text); i++) {
        if (i == targetIndex) {
            metrics.x = currentX;
            metrics.y = currentY;
            metrics.row = metrics.lineCount - 1;
            metrics.col = currentX > TEXT_START_X ? (currentX - TEXT_START_X) / SPACE_WIDTH : 0; // Approximate column
        }

        if (text[i] == '\n') {
            metrics.maxLineWidth = currentLineWidth > metrics.maxLineWidth ? currentLineWidth : metrics.maxLineWidth;
            currentY += LINE_HEIGHT;
            currentX = TEXT_START_X;
            currentLineWidth = 0;
            metrics.lineCount++;
            continue;
        }

        char ch[2] = {text[i], '\0'};
        int charWidth = (text[i] == ' ' ? SPACE_WIDTH : MeasureText(ch, 30) + 3);
        currentX += charWidth;
        currentLineWidth += charWidth;
    }

    // Handle metrics for the index at the very end of the text
    if (targetIndex == strlen(text)) {
        metrics.x = currentX;
        metrics.y = currentY;
        metrics.row = metrics.lineCount - 1;
        metrics.col = currentX > TEXT_START_X ? (currentX - TEXT_START_X) / SPACE_WIDTH : 0;
    }
    
    // Final max line width check after the loop
    metrics.maxLineWidth = currentLineWidth > metrics.maxLineWidth ? currentLineWidth : metrics.maxLineWidth;
    
    // If text is empty
    if (strlen(text) == 0) {
        metrics.lineCount = 1;
        metrics.x = TEXT_START_X;
        metrics.y = TEXT_START_Y;
    } else {
        // Increment line count for the last line if it wasn't ended by a newline
        if (text[strlen(text) - 1] != '\n') {
            metrics.lineCount++;
        }
    }


    return metrics;
}


// =================================================================================
// Helper function to calculate the character index at the given mouse position
// The logic is adjusted to account for scroll offsets
// =================================================================================
int get_index_at_mouse_position(float mx, float my, const char* text) {
    // Adjust mouse coordinates by scroll offset
    float adjusted_my = my + scrollOffsetY;
    float adjusted_mx = mx + scrollOffsetX;

    int line = (adjusted_my - TEXT_START_Y) / LINE_HEIGHT;
    if (line < 0) line = 0;

    int lineStart = 0, currentLine = 0;
    int textLen = strlen(text);

    // 1. Find the start index of the target line
    for (int i = 0; i < textLen; i++) {
        if (currentLine == line) break;
        if (text[i] == '\n') {
            currentLine++;
            lineStart = i + 1;
        }
    }

    // If the mouse clicked below the text area, snap to the end of the text
    // The total number of lines is lineCount from getTextMetrics
    if (line >= getTextMetrics(textLen, text).lineCount) {
        return textLen;
    }


    // 2. Determine the character index on that line based on x position
    int xOffset = TEXT_START_X;
    int newCursor = lineStart;

    for (int i = lineStart; i < textLen; i++) {
        if (text[i] == '\n') break;
        char ch[2] = {text[i], '\0'};
        
        int charWidth = (text[i] == ' ' ? SPACE_WIDTH : MeasureText(ch, 30) + 3);

        // Check if the adjusted mouse x is past the midpoint of the character
        if (xOffset + charWidth / 2 >= adjusted_mx) break;
        
        xOffset += charWidth;
        newCursor++;
    }
    return newCursor;
}


// =================================================================================
// Function to save, compile, and run the shared C code
// (No change needed here, as it's not visual)
// =================================================================================
void save_compile_and_run(char str[]){
    FILE *fptr;
    const char *filename = "file.c";

    // 1. Save the sharedText content to the file. Use "w" mode to overwrite previous content.
    fptr = fopen(filename, "w");

    if (fptr == NULL) {
        printf("Error: Could not open file %s for writing.\n", filename);
    } else {
        fprintf(fptr, "%s", str);
        fclose(fptr);
        printf("\n✅ Content saved to '%s'.\n", filename);

        // 2. Compile the saved C file
        printf("Attempting to compile the code...\n");
        int compile_status = system("gcc file.c -o notepad_code -lm"); 
        
        if (compile_status == 0) {
            printf("✅ Compilation successful. Running the generated executable...\n");
            int run_status = system("./notepad_code");

            if (run_status != 0) {
                printf("Error: Program execution finished with status code %d.\n", run_status);
            }
        } else {
            printf("Error: Compilation failed. Check your code in '%s' for errors.\n", filename);
        }
    }
}

// =================================================================================
// Send edit or cursor update to server
// (No change needed)
// =================================================================================
void send_update(char action, int pos, char ch) {
    char buf[32];
    if(action == 'I') snprintf(buf, sizeof(buf), "I|%d|%c", pos, ch);
    else if(action == 'D') snprintf(buf, sizeof(buf), "D|%d", pos);
    else if(action == 'C') snprintf(buf, sizeof(buf), "C|%d", pos);
    
    if (strlen(buf) > 0) { 
        send(sock, buf, strlen(buf), 0);
    }
}

// =================================================================================
// Receiver thread
// (No change needed)
// =================================================================================
void *receive_thread(void *arg) {
    char buf[BUFFER_SIZE*2];
    int bytes;
    while((bytes = recv(sock, buf, sizeof(buf)-1, 0)) > 0){
        buf[bytes]='\0';

        pthread_mutex_lock(&lock);
        
        char oldText[BUFFER_SIZE] = {0};
        strncpy(oldText, sharedText, BUFFER_SIZE-1);
        
        if (strncmp(buf, "INIT|", 5) == 0) {
            myColorIndex = atoi(buf + 5);
            pthread_mutex_unlock(&lock);
            continue; 
        }
        
        char *cursorSection = strstr(buf,"|CURSORS");
        if(cursorSection){
            *cursorSection = '\0';
            
            strncpy(sharedText, buf, BUFFER_SIZE-1);
            sharedText[BUFFER_SIZE-1] = '\0';
            
            bool textContentChanged = (strcmp(oldText, sharedText) != 0);

            char *colorsSection = strstr(cursorSection + strlen("|CURSORS"),"|COLORS");
            
            if(colorsSection){
                *colorsSection = '\0';
                char *cursors_str = cursorSection + strlen("|CURSORS");
                char *colors_str = colorsSection + strlen("|COLORS");

                char cursors_copy[200];
                strncpy(cursors_copy, cursors_str, sizeof(cursors_copy)-1);
                cursors_copy[sizeof(cursors_copy)-1] = '\0';
                
                otherCount=0;
                char *t = strtok(cursors_copy, "|");
                while(t && otherCount < MAX_CLIENTS){
                    otherCursors[otherCount++] = atoi(t);
                    t = strtok(NULL,"|");
                }

                char colors_copy[200];
                strncpy(colors_copy, colors_str, sizeof(colors_copy)-1);
                colors_copy[sizeof(colors_copy)-1] = '\0';

                int i=0;
                t = strtok(colors_copy,"|");
                while(t && i<otherCount){
                    otherColors[i++] = atoi(t);
                    t = strtok(NULL,"|");
                }
                
                int newCursorFromServer = -1;
                for (int i = 0; i < otherCount; i++) {
                    if (otherColors[i] == myColorIndex) {
                        newCursorFromServer = otherCursors[i];
                        break;
                    }
                }
                
                if (newCursorFromServer != -1) {
                    cursorPos = newCursorFromServer;
                    
                    if (textContentChanged) {
                        selectionStart = cursorPos;
                        selectionEnd = cursorPos;
                        isSelecting = false;
                    } else {
                        selectionEnd = cursorPos;
                    }
                }
            }
        } else {
            strncpy(sharedText, buf, BUFFER_SIZE-1);
            sharedText[BUFFER_SIZE-1] = '\0';
            
            selectionStart = cursorPos;
            selectionEnd = cursorPos;
            isSelecting = false;
        }

        if (cursorPos > strlen(sharedText)) cursorPos = strlen(sharedText);

        pthread_mutex_unlock(&lock);
    }
    printf("Server disconnected.\n");
    exit(0);
}

// =================================================================================
// Map color index to Raylib Color
// (No change needed)
// =================================================================================
Color getColor(int idx){
    switch(idx%10){
        case 0: return BLUE;
        case 1: return GREEN;
        case 2: return ORANGE;
        case 3: return MAGENTA;
        case 4: return PINK;
        case 5: return YELLOW;
        case 6: return VIOLET;
        case 7: return BROWN;
        case 8: return PURPLE;
        case 9: return GRAY;
        default: return BLACK;
    }
}

// =================================================================================
// Function to handle selection deletion
// (No change needed)
// =================================================================================
bool delete_selection(int* cursor_moved, bool* edit_sent_this_frame) {
    if (selectionStart != selectionEnd) {
        int minIdx = (selectionStart < selectionEnd) ? selectionStart : selectionEnd;
        int maxIdx = (selectionStart > selectionEnd) ? selectionStart : selectionEnd;
        int len = maxIdx - minIdx;
        
        if (len > 0) {
            memmove(sharedText + minIdx, sharedText + maxIdx, strlen(sharedText) - maxIdx + 1);

            for(int i = 0; i < len; i++) {
                send_update('D', minIdx, 0); 
                usleep(1000); // 1ms delay
            }
            
            cursorPos = minIdx;
            *edit_sent_this_frame = true;
        }
        selectionStart = cursorPos;
        selectionEnd = cursorPos;
        *cursor_moved = 1; 
        return true;
    }
    return false;
}

// =================================================================================
// Helper function to manage auto-scrolling to keep the cursor visible
// =================================================================================
void keep_cursor_visible(int screenWidth, int screenHeight, TextMetrics cursorMetrics) {
    // Area for text drawing (excluding status bar, top bar, and scrollbars)
    int textRegionY = TEXT_START_Y;
    int textRegionH = screenHeight - TEXT_START_Y - (0.03 * screenHeight) - SCROLLBAR_WIDTH; // Account for status/h-bar
    int textRegionX = TEXT_START_X;
    int textRegionW = screenWidth - TEXT_START_X - SCROLLBAR_WIDTH; // Account for v-bar

    // Vertical Scroll
    int cursorYRel = cursorMetrics.y - scrollOffsetY;

    // Check if cursor is above the visible area
    if (cursorYRel < textRegionY) {
        scrollOffsetY -= (textRegionY - cursorYRel);
    }
    // Check if cursor is below the visible area
    else if (cursorYRel + LINE_HEIGHT > textRegionY + textRegionH) {
        scrollOffsetY += (cursorYRel + LINE_HEIGHT - (textRegionY + textRegionH));
    }
    
    // Horizontal Scroll
    int cursorXRel = cursorMetrics.x - scrollOffsetX;

    // Check if cursor is to the left of the visible area
    if (cursorXRel < textRegionX) {
        scrollOffsetX -= (textRegionX - cursorXRel);
    }
    // Check if cursor is to the right of the visible area
    else if (cursorXRel > textRegionX + textRegionW) {
        scrollOffsetX += (cursorXRel - (textRegionX + textRegionW));
    }
}

// =================================================================================
// Main loop for input and drawing
// =================================================================================
int main() {
    // ... (Initialization and Authentication code - same as original) ...
    pthread_mutex_init(&lock,NULL);

    sock = socket(AF_INET, SOCK_STREAM,0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET; 
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if(connect(sock,(struct sockaddr*)&server_addr,sizeof(server_addr))<0){
        perror("Connect failed"); return 1;
    }
    printf("✅ Connected to server. Attempting authentication...\n");
    
    char password[128];
    printf("Enter Password: ");
    if (scanf("%127s", password) != 1) {
        printf("Failed to read password.\n");
        close(sock);
        return -1;
    }
    
    send(sock, password, strlen(password) + 1, 0); 
    
    char reply[32];
    int reply_bytes = recv(sock, reply, sizeof(reply) - 1, 0);
    
    if (reply_bytes <= 0) {
        printf("Error during authentication reply or server disconnected.\n");
        close(sock);
        return -1;
    }
    
    reply[reply_bytes] = '\0';
    
    if (strncmp(reply, "AUTH OK", 7) != 0) {
        printf("Authentication failed. Server reply: '%s'. Exiting.\n", reply);
        close(sock);
        return -1;
    }
    printf("✅ Authentication successful.\n");

    pthread_t tid;
    pthread_create(&tid,NULL,receive_thread,NULL);

    InitWindow(1280,720,"MindMeld Collaborative Notepad");
    SetTargetFPS(60);

    int cursor_moved = 0; 
    bool edit_sent_this_frame = false; 
    static int desiredCol = -1;

    // Scrollbar interaction state
    bool draggingVScroll = false;
    bool draggingHScroll = false;

    while(!WindowShouldClose()){
        cursor_moved = 0; 
        edit_sent_this_frame = false; 
        int oldCursorPos = cursorPos;
        
        bool isModifyingKey = (GetKeyPressed() > 0 || IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ENTER));
        
        int screenWidth = GetScreenWidth();
        int screenHeight = GetScreenHeight();
        int statusBarHeight = 0.03 * screenHeight;

        // Calculate viewable area and max scroll limits
        float viewH = screenHeight - TEXT_START_Y - statusBarHeight - SCROLLBAR_WIDTH;
        float viewW = screenWidth - TEXT_START_X - SCROLLBAR_WIDTH;

        pthread_mutex_lock(&lock);
        TextMetrics fullMetrics = getTextMetrics(strlen(sharedText), sharedText);
        TextMetrics cursorMetrics = getTextMetrics(cursorPos, sharedText);

        // Max possible scroll
        float maxScrollY = (fullMetrics.lineCount * LINE_HEIGHT + TEXT_START_Y) - (viewH + TEXT_START_Y);
        if (maxScrollY < 0) maxScrollY = 0;
        
        float maxScrollX = fullMetrics.maxLineWidth - viewW;
        if (maxScrollX < 0) maxScrollX = 0;
        
        // Clamp scroll offsets
        if (scrollOffsetY < 0) scrollOffsetY = 0;
        if (scrollOffsetY > maxScrollY) scrollOffsetY = maxScrollY;

        if (scrollOffsetX < 0) scrollOffsetX = 0;
        if (scrollOffsetX > maxScrollX) scrollOffsetX = maxScrollX;


        // --- Mouse Input Handling (Scrollbar interaction) ---
        Vector2 mp = GetMousePosition();
        
        // Vertical Scrollbar Logic
        Rectangle vScrollRect = { (float)screenWidth - SCROLLBAR_WIDTH, (float)TEXT_START_Y, (float)SCROLLBAR_WIDTH, viewH };
        float vScrollThumbHeight = (viewH / (maxScrollY + viewH)) * viewH;
        float vScrollThumbY = TEXT_START_Y + (scrollOffsetY / maxScrollY) * (viewH - vScrollThumbHeight);
        Rectangle vScrollThumbRect = { vScrollRect.x, vScrollThumbY, vScrollRect.width, vScrollThumbHeight };

        if (maxScrollY > 0) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mp, vScrollThumbRect)) {
                draggingVScroll = true;
            } else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mp, vScrollRect)) {
                // Click in track to jump
                if (mp.y < vScrollThumbY) {
                    scrollOffsetY -= viewH;
                } else {
                    scrollOffsetY += viewH;
                }
                // Recalculate and clamp
                if (scrollOffsetY < 0) scrollOffsetY = 0;
                if (scrollOffsetY > maxScrollY) scrollOffsetY = maxScrollY;
            }
        }
        
        // Horizontal Scrollbar Logic
        Rectangle hScrollRect = { (float)TEXT_START_X, screenHeight - statusBarHeight - SCROLLBAR_WIDTH, viewW, (float)SCROLLBAR_WIDTH };
        float hScrollThumbWidth = (viewW / (maxScrollX + viewW)) * viewW;
        float hScrollThumbX = TEXT_START_X + (scrollOffsetX / maxScrollX) * (viewW - hScrollThumbWidth);
        Rectangle hScrollThumbRect = { hScrollThumbX, hScrollRect.y, hScrollThumbWidth, hScrollRect.height };

        if (maxScrollX > 0) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mp, hScrollThumbRect)) {
                draggingHScroll = true;
            } else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mp, hScrollRect)) {
                // Click in track to jump
                if (mp.x < hScrollThumbX) {
                    scrollOffsetX -= viewW;
                } else {
                    scrollOffsetX += viewW;
                }
                // Recalculate and clamp
                if (scrollOffsetX < 0) scrollOffsetX = 0;
                if (scrollOffsetX > maxScrollX) scrollOffsetX = maxScrollX;
            }
        }

        // Dragging update
        if (draggingVScroll) {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                float thumbCenterY = vScrollThumbY + vScrollThumbHeight / 2.0f;
                float deltaY = mp.y - thumbCenterY;
                float normalizedDelta = deltaY / (viewH - vScrollThumbHeight);
                
                scrollOffsetY += normalizedDelta * maxScrollY * 0.5f; // Factor of 0.5 for smoother dragging

                if (scrollOffsetY < 0) scrollOffsetY = 0;
                if (scrollOffsetY > maxScrollY) scrollOffsetY = maxScrollY;
            } else {
                draggingVScroll = false;
            }
        }

        if (draggingHScroll) {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                float thumbCenterX = hScrollThumbX + hScrollThumbWidth / 2.0f;
                float deltaX = mp.x - thumbCenterX;
                float normalizedDelta = deltaX / (viewW - hScrollThumbWidth);
                
                scrollOffsetX += normalizedDelta * maxScrollX * 0.5f; // Factor of 0.5 for smoother dragging

                if (scrollOffsetX < 0) scrollOffsetX = 0;
                if (scrollOffsetX > maxScrollX) scrollOffsetX = maxScrollX;
            } else {
                draggingHScroll = false;
            }
        }

        // Mouse Wheel Scroll (only when not dragging a thumb)
        float wheelMove = GetMouseWheelMove();
        if (wheelMove != 0 && !draggingVScroll) {
            scrollOffsetY -= wheelMove * SCROLL_SPEED_V;
        }

        // Re-clamp scroll offsets after mouse wheel/track jump
        if (scrollOffsetY < 0) scrollOffsetY = 0;
        if (scrollOffsetY > maxScrollY) scrollOffsetY = maxScrollY;
        if (scrollOffsetX < 0) scrollOffsetX = 0;
        if (scrollOffsetX > maxScrollX) maxScrollX = maxScrollX;

        // If a scrollbar is active, skip regular text mouse input
        bool interactingWithScrollbars = draggingVScroll || draggingHScroll || CheckCollisionPointRec(mp, vScrollRect) || CheckCollisionPointRec(mp, hScrollRect);
        
        if (!interactingWithScrollbars) {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                
                int clickedPos = get_index_at_mouse_position(mp.x, mp.y, sharedText);
                
                cursorPos = clickedPos;

                if (!shiftHeld) {
                    selectionStart = clickedPos;
                }
                selectionEnd = clickedPos; 
                
                isSelecting = true;
                cursor_moved = 1; 
                desiredCol = -1; 
            } 
            
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && isSelecting) {
                int newEnd = get_index_at_mouse_position(mp.x, mp.y, sharedText);
                if (newEnd != selectionEnd) {
                    selectionEnd = newEnd;
                    cursorPos = newEnd; 
                    cursor_moved = 1; 
                }
            } 
            
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                isSelecting = false;
                cursor_moved = 1; 
            }
        }


        // --- Global Selection Deletion Before Any Edit (including pressing a letter) ---
        if (isModifyingKey) {
            delete_selection(&cursor_moved, &edit_sent_this_frame);
        }

        // --- Character input (Same logic)
        // ... (Character, Backspace, Delete, Tab, Enter logic - same as original) ...
        int key = GetCharPressed();
        while(key>0){
            if(key>=32 && key<=125 && strlen(sharedText) < BUFFER_SIZE - 1){
                for(int i=strlen(sharedText);i>=cursorPos;i--) sharedText[i+1]=sharedText[i];
                sharedText[cursorPos]=(char)key;
                
                send_update('I',cursorPos,(char)key);
                cursorPos++;
                selectionStart = cursorPos; 
                selectionEnd = cursorPos;
                cursor_moved = 1; 
                edit_sent_this_frame = true; 
                desiredCol = -1;
            }
            key=GetCharPressed();
        }
        
        if(IsKeyPressed(KEY_BACKSPACE) && cursorPos>0 && selectionStart == selectionEnd && !edit_sent_this_frame){
            for(int i=cursorPos-1;i<strlen(sharedText);i++) sharedText[i]=sharedText[i+1];
            
            cursorPos--;
            send_update('D',cursorPos,0);
            selectionStart = cursorPos;
            selectionEnd = cursorPos;
            cursor_moved = 1; 
            edit_sent_this_frame = true; 
            desiredCol = -1; 
        }
        
        if(IsKeyPressed(KEY_DELETE) && cursorPos < strlen(sharedText) && selectionStart == selectionEnd && !edit_sent_this_frame){
            for(int i=cursorPos; i<strlen(sharedText); i++) sharedText[i] = sharedText[i+1];
            
            send_update('D', cursorPos, 0); 
            
            selectionStart = cursorPos;
            selectionEnd = cursorPos;
            cursor_moved = 1; 
            edit_sent_this_frame = true; 
            desiredCol = -1; 
        }
        
        int tab_size = 4;
        if(IsKeyPressed(KEY_TAB) && (strlen(sharedText) + tab_size) < BUFFER_SIZE - 1){
            for(int k=0; k<tab_size; k++){
                for(int i=strlen(sharedText); i>=cursorPos; i--) sharedText[i+1] = sharedText[i];
                sharedText[cursorPos] = ' ';
                send_update('I', cursorPos, ' '); 
                cursorPos++; 
            }
            selectionStart = cursorPos;
            selectionEnd = cursorPos;
            cursor_moved = 1; 
            edit_sent_this_frame = true; 
            desiredCol = -1; 
        }

        if(IsKeyPressed(KEY_ENTER) && strlen(sharedText) < BUFFER_SIZE - 1){
            for(int i=strlen(sharedText);i>=cursorPos;i--) sharedText[i+1]=sharedText[i];
            sharedText[cursorPos]='\n';
            
            send_update('I',cursorPos,'\n');
            cursorPos++;
            selectionStart = cursorPos;
            selectionEnd = cursorPos;
            cursor_moved = 1; 
            edit_sent_this_frame = true; 
            desiredCol = -1; 
        }

        // --- Keyboard Cursor Movement and Selection (Shift + Arrows) ---
        bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        bool cursorMovedByKeyboard = false;

        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT)) {
            desiredCol = -1; 
            if (IsKeyPressed(KEY_LEFT)) {
                if (!shiftHeld && selectionStart != selectionEnd) {
                    cursorPos = (selectionStart < selectionEnd) ? selectionStart : selectionEnd;
                } else if (cursorPos > 0) {
                    cursorPos--;
                }
            }
            if (IsKeyPressed(KEY_RIGHT)) {
                if (!shiftHeld && selectionStart != selectionEnd) {
                    cursorPos = (selectionStart > selectionEnd) ? selectionStart : selectionEnd;
                } else if (cursorPos < strlen(sharedText)) {
                    cursorPos++;
                }
            }
            cursorMovedByKeyboard = true;
        }
        
        if(IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_DOWN)){
            if (!shiftHeld && selectionStart != selectionEnd) {
                cursorPos = selectionEnd; 
            }

            if (desiredCol == -1) {
                TextMetrics currentMetrics = getTextMetrics(cursorPos, sharedText);
                desiredCol = currentMetrics.col; 
            }
            
            // Re-calculate TextMetrics after a change to desiredCol
            cursorMetrics = getTextMetrics(cursorPos, sharedText); 

            if(IsKeyPressed(KEY_UP)){
                int currentLineStart = 0;
                for(int i = cursorPos - 1; i >= 0; i--) {
                    if (sharedText[i] == '\n') {
                        currentLineStart = i + 1;
                        break;
                    }
                }
                
                int prevLineEnd = currentLineStart > 0 ? currentLineStart - 1 : -1;

                if(prevLineEnd > 0){
                    int prevLineStart = 0;
                    for(int i = prevLineEnd - 1; i >= 0; i--) {
                        if (sharedText[i] == '\n') {
                            prevLineStart = i + 1;
                            break;
                        }
                    }
                    
                    int targetPos = prevLineStart + desiredCol;
                    int actualPrevLineEnd = currentLineStart - 1;
                    
                    if (targetPos > actualPrevLineEnd) {
                        cursorPos = actualPrevLineEnd;
                    } else {
                        cursorPos = targetPos;
                    }
                    cursorMovedByKeyboard = true;
                } else if (cursorPos > 0) {
                    cursorPos = 0;
                    cursorMovedByKeyboard = true;
                }
            }

            if(IsKeyPressed(KEY_DOWN)){
                int currentLineStart = 0;
                for(int i = cursorPos - 1; i >= 0; i--) {
                    if (sharedText[i] == '\n') {
                        currentLineStart = i + 1;
                        break;
                    }
                }

                int nextLineStart = currentLineStart;
                while(nextLineStart < strlen(sharedText) && sharedText[nextLineStart] != '\n') nextLineStart++;

                if(nextLineStart < strlen(sharedText)){
                    nextLineStart++; 
                    
                    int nextLineEnd = nextLineStart;
                    while(nextLineEnd < strlen(sharedText) && sharedText[nextLineEnd] != '\n') nextLineEnd++;
                    
                    int targetPos = nextLineStart + desiredCol;
                    
                    if (targetPos > nextLineEnd) {
                        cursorPos = nextLineEnd;
                    } else {
                        cursorPos = targetPos;
                    }
                    cursorMovedByKeyboard = true;
                } else {
                    cursorPos = strlen(sharedText);
                    cursorMovedByKeyboard = true;
                }
            }
        }
        
        if (cursorMovedByKeyboard) {
            if (shiftHeld) {
                selectionEnd = cursorPos;
            } else {
                selectionStart = cursorPos;
                selectionEnd = cursorPos;
            }
            // After keyboard move, update metrics and auto-scroll
            cursorMetrics = getTextMetrics(cursorPos, sharedText); 
            keep_cursor_visible(screenWidth, screenHeight, cursorMetrics); 
        }
        
        // Final check for cursor movement
        if (cursorPos != oldCursorPos) {
            cursor_moved = 1;
            // Also call auto-scroll on any cursor movement (including mouse/edit)
            cursorMetrics = getTextMetrics(cursorPos, sharedText);
            keep_cursor_visible(screenWidth, screenHeight, cursorMetrics);
        }
        
        pthread_mutex_unlock(&lock);
        
        // --- Send Cursor Update ---
        if (cursor_moved && !edit_sent_this_frame) {
            send_update('C', cursorPos, 0); 
        }

        // --- Compile Button Click ---
        Rectangle compileButton = {700, 40, 400, 50};
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), compileButton)) {
            // Check again that we are not interacting with the scrollbars, 
            // though the collision check should mostly prevent this.
            if (!CheckCollisionPointRec(mp, vScrollRect) && !CheckCollisionPointRec(mp, hScrollRect)) {
                pthread_mutex_lock(&lock);
                save_compile_and_run(sharedText);
                pthread_mutex_unlock(&lock);
            }
        }

        // --- Drawing
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("MindMeld Collaborative Notepad",20,20,40,DARKGRAY);

        // Define the text drawing area
        Rectangle textClipRect = { TEXT_START_X - 5, TEXT_START_Y, viewW + 5, viewH + 5};
        
        // Start scissor mode to clip text and cursors
        BeginScissorMode((int)textClipRect.x, (int)textClipRect.y, (int)textClipRect.width, (int)textClipRect.height);
        
        pthread_mutex_lock(&lock);
        
        // Recalculate full metrics for accurate drawing
        fullMetrics = getTextMetrics(strlen(sharedText), sharedText);

        // --- Draw Selection Highlight ---
        int minIdx = (selectionStart < selectionEnd) ? selectionStart : selectionEnd;
        int maxIdx = (selectionStart > selectionEnd) ? selectionStart : selectionEnd;
        
        if (minIdx != maxIdx) {
            int currentX = TEXT_START_X - (int)scrollOffsetX;
            int currentY = TEXT_START_Y - (int)scrollOffsetY;
            
            for (int i = 0; i < strlen(sharedText); i++) {
                if (sharedText[i] == '\n') {
                    currentY += LINE_HEIGHT;
                    currentX = TEXT_START_X - (int)scrollOffsetX;
                    continue;
                }
                
                char ch[2] = {sharedText[i], '\0'};
                int charWidth = (sharedText[i] == ' ' ? SPACE_WIDTH : MeasureText(ch, 30) + 3);

                if (i >= minIdx && i < maxIdx) {
                    DrawRectangle(currentX, currentY, charWidth, LINE_HEIGHT, Fade(BLUE, 0.4f));
                }

                currentX += charWidth;
            }
        }
        
        // --- Draw Text and Line Numbers ---
        int currentX = TEXT_START_X - (int)scrollOffsetX;
        int currentY = TEXT_START_Y - (int)scrollOffsetY;
        int lineNum = 1;

        // Draw Line Numbers (Outside of the text clipping area to the left, but still scrolled vertically)
        EndScissorMode(); // Stop clipping to draw line numbers
        for(int i = 0; i < fullMetrics.lineCount; i++) {
            int lnY = TEXT_START_Y + i * LINE_HEIGHT - (int)scrollOffsetY;
            
            // Only draw if line number is within the vertical clip bounds
            if (lnY + LINE_HEIGHT > TEXT_START_Y && lnY < TEXT_START_Y + viewH) {
                DrawText(TextFormat("%d", i+1), 15, lnY + 6, 20, LIGHTGRAY);
            }
        }
        
        BeginScissorMode((int)textClipRect.x, (int)textClipRect.y, (int)textClipRect.width, (int)textClipRect.height); // Restart clipping

        // Draw the text itself, character by character (required for custom space width)
        for (int i = 0; i < strlen(sharedText); i++) {
            if (sharedText[i] == '\n') {
                currentY += LINE_HEIGHT;
                currentX = TEXT_START_X - (int)scrollOffsetX;
                lineNum++;
                continue;
            }

            char ch[2] = {sharedText[i], '\0'};
            int charWidth = (sharedText[i] == ' ' ? SPACE_WIDTH : MeasureText(ch, 30) + 3);
            
            DrawText(ch, currentX, currentY, 30, BLACK);
            currentX += charWidth;
        }


        // --- Draw Cursors ---
        cursorMetrics = getTextMetrics(cursorPos, sharedText);

        // Current user cursor (yours)
        int myX = cursorMetrics.x - (int)scrollOffsetX;
        int myY = cursorMetrics.y - (int)scrollOffsetY;
        
        DrawRectangle(TEXT_START_X, myY, viewW, LINE_HEIGHT, Fade(getColor(myColorIndex),0.1f)); 
        if(((int)(GetTime()*2))%2==0) DrawRectangle(myX, myY, 2, 30, getColor(myColorIndex));

        // Other clients cursors
        for(int i=0;i<otherCount;i++){
            if(otherColors[i] == myColorIndex) continue; 

            TextMetrics otherMetrics = getTextMetrics(otherCursors[i], sharedText);
            
            int otherX = otherMetrics.x - (int)scrollOffsetX;
            int otherY = otherMetrics.y - (int)scrollOffsetY;

            Color c = getColor(otherColors[i]);
            DrawRectangle(TEXT_START_X, otherY, viewW, LINE_HEIGHT, Fade(c,0.1f)); // highlight line
            DrawRectangle(otherX, otherY, 2, 30, c);              // cursor
        }

        EndScissorMode(); // Stop clipping for everything else

        // --- Draw Compile Button (with visual feedback) ---
        Color buttonColor = GRAY;
        if (CheckCollisionPointRec(GetMousePosition(), compileButton)) {
            buttonColor = LIGHTGRAY; 
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                buttonColor = DARKGRAY; 
            }
        }
        DrawRectangleRec(compileButton, buttonColor);
        DrawText("Save and Compile Code", 710, 50, 30, BLACK);

        // --- Status Bar Rectangle at the Bottom ---
        DrawRectangle(0, screenHeight - statusBarHeight, screenWidth, statusBarHeight, DARKGRAY);
        DrawText(TextFormat("Total Lines: %d  Line: %d Column %d", fullMetrics.lineCount, cursorMetrics.row+1, cursorMetrics.col+1), screenWidth*0.02, screenHeight - statusBarHeight + 0.05*statusBarHeight, statusBarHeight, RAYWHITE);
        
        // --- Draw Scrollbars ---
        
        // Vertical Scrollbar (Track)
        DrawRectangleRec(vScrollRect, Fade(DARKGRAY, 0.2f));
        // Vertical Scrollbar (Thumb)
        if (maxScrollY > 0) {
            Color thumbColor = GRAY;
            if (draggingVScroll) thumbColor = DARKGRAY;
            DrawRectangleRec(vScrollThumbRect, thumbColor);
        }

        // Horizontal Scrollbar (Track)
        DrawRectangleRec(hScrollRect, Fade(DARKGRAY, 0.2f));
        // Horizontal Scrollbar (Thumb)
        if (maxScrollX > 0) {
            Color thumbColor = GRAY;
            if (draggingHScroll) thumbColor = DARKGRAY;
            DrawRectangleRec(hScrollThumbRect, thumbColor);
        }

        // Corner block where the two scrollbars meet
        DrawRectangle(screenWidth - SCROLLBAR_WIDTH, screenHeight - statusBarHeight - SCROLLBAR_WIDTH, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH, DARKGRAY);

        pthread_mutex_unlock(&lock);
        EndDrawing();
    }

    close(sock);
    pthread_mutex_destroy(&lock);
    CloseWindow();
    return 0;
}