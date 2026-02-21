🚀 Features
Centralized Server: Handles multiple client connections using TCP sockets.

GTK Interface: A clean, responsive user interface built with the GTK library.

Database Integration: Persistent storage of chat logs and user data using a dedicated database module.

Modular Design: Separated logic for database management, server operations, and client UI.

📂 Project Structure
server.c: The backbone of the application; manages connections and routes messages.

client.c: The GTK-based frontend that allows users to send and receive messages.

chat_db.c / chat_db.h: The database abstraction layer for saving and retrieving chat history.

🛠️ Prerequisites
Before building, ensure you have the following installed:

gcc (C compiler)

GTK 3.0 or GTK 4.0 development headers
