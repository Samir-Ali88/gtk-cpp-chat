This is a custom-built, client-side chat application developed in C++. It connects to a centralized server using raw TCP sockets to enable real-time communication. The graphical user interface was built from scratch using the GTK3 library and styled with custom CSS.

Key Features:

Multithreaded Architecture: Uses std::thread to separate the network receiving loop from the GTK main UI loop, ensuring the application never freezes while waiting for server data.

Custom Protocol Parsing: Interprets a custom text-based application protocol to handle system alerts (SERVER:), private whispers (PRIVATE:), and channel navigation.

State Management: Caches private chat histories in memory using standard C++ containers (std::vector), allowing users to switch seamlessly between global channels and private direct messages.

Dynamic UI: Features auto-scrolling, visual notification badges for unread background messages, and dynamically generated user-list menus.
