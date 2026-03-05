# MindMeld: Collaborative Code Editor

**MindMeld** is a real-time, multi-user collaborative text editor designed for synchronized C programming. It features a custom-built GUI using **Raylib**, a robust **C Sockets** backend for communication, and a basic implementation of **Operational Transformation (OT)** to ensure consistency across multiple clients.

---

## 🚀 Features

### Core Editor Functionality
* **Real-time Collaboration:** Multiple users can edit the same document simultaneously with low latency.
* **Visual Cursors:** Each participant is assigned a unique color; you can see other users' cursors and active line highlights in real-time.
* **Operational Transformation (OT):** The server intelligently adjusts cursor positions when other users insert or delete text to prevent "cursor drifting."
* **Syntax-Ready Rendering:** Custom character-by-character rendering with support for tabs and consistent spacing.

### Advanced UI/UX
* **Smooth Scrolling:** Integrated vertical and horizontal scrollbars with mouse-wheel support for handling large files.
* **Text Selection:** Support for mouse-based selection and keyboard-based selection (Shift + Arrows) for block deletion.
* **Line Numbering:** Dynamic line numbers that stay synchronized with the vertical scroll.
* **Auto-Scroll:** The view automatically follows your cursor as you type or move past the viewport boundaries.

### Integrated Development
* **Remote Compilation:** A dedicated **"Save and Compile"** button that:
    1.  Saves the current shared buffer to a local `file.c`.
    2.  Invokes `gcc` to compile the code.
    3.  Runs the generated executable directly from the editor environment.

---

## 🛠️ Tech Stack

* **Language:** C (C11)
* **Graphics:** [Raylib](https://www.raylib.com/)
* **Networking:** POSIX Sockets (TCP)
* **Concurrency:** POSIX Threads (`pthread`)
* **Build System:** GCC

---

## 📦 Installation & Setup

### 1. Prerequisites
Ensure you have the following installed on your Linux system:
* `gcc`
* `raylib` (development headers and library)
* `pthreads`

### 2. Server Configuration
1.  Open `server.c`.
2.  Modify `#define AUTH_PASSWORD` if you wish to change the connection password (default: `secret123`).
3.  Compile and run:
    ```bash
    gcc server.c -o server -lpthread
    ./server
    ```

### 3. Client Configuration
1.  Open `client.c`.
2.  Update `#define SERVER_IP` with the IP address of the machine running the server.
3.  Compile and run:
    ```bash
    gcc client.c -o client -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
    ./client
    ```

---

## 🖥️ How to Use

1.  **Authentication:** Upon launching the client, enter the password configured in the server.
2.  **Editing:** Type naturally. Use `Tab` for indentation (4 spaces) and `Enter` for new lines.
3.  **Selection:** Click and drag with the mouse or hold `Shift` while using arrow keys to select text. Press `Backspace` or `Delete` to remove the selection.
4.  **Navigation:** Use the scrollbars or mouse wheel to navigate large files.
5.  **Execution:** Click the **"Save and Compile Code"** button to test your C code. The output will be visible in the terminal where the client is running.

---

## 📐 Architecture Overview

### Data Protocol
Communication follows a piped format over TCP:
* **Insert:** `I|position|character`
* **Delete:** `D|position`
* **Cursor:** `C|position`

### Operational Transformation (OT)
The server maintains the "source of truth." When a client performs an action:
1.  The server applies the change to the global buffer.
2.  The server iterates through all other active clients and transforms their cursor positions based on the edit location.
3.  The server broadcasts the updated buffer and transformed cursors to all participants to ensure everyone stays in sync.

---
