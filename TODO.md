## TODO

1. Given that each connection occupies 2 file descriptors, this server can handle no more than around 500 connections without exhausting more than 1024. This means: we need a way to reject incoming connections if the server reached that limit, instead of just crashing. Maybe reject with a 529.
2. Refactor this code:
  - Move set\_nonblocking() and get\_in\_addr() to a utils.h file.
  - In main.c, extract into a function within main.c all the logic that is used to finally bind() to the sockfd. This function also frees the servinfo, and it returns the necessary values for the rest of the code to work. If many things need to be returned, then create a struct that contains all these values.
  - The event loop must be extracted to a function, and each part of the big if-else clause (server listening, epollin, epollout) has to be also on their own function.
3. Fix this bug: for big files, sendfile() does not write everything in one go to the client, meaning we do not need to close immediately the connection. We need to keep track of the offset, feel free to modify existing client datastructure if it helps to track state and close connection only when the whole file was written.
   
