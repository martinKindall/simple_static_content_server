## TODO

1. Given that each connection occupies 2 file descriptors, this server can handle no more than around 500 connections without exhausting more than 1024. This means: we need a way to reject incoming connections if the server reached that limit, instead of just crashing. Maybe reject with a 529.
2. Refactor this code:
  2.1 Move set\_nonblocking() and get\_in\_addr() to a utils.h file.
  2.2 In main.c, extract into a function within main.c all the logic that is used to finally bind() to the sockfd. This function also frees the servinfo, and it returns the necessary values for the rest of the code to work. If many things need to be returned, then create a struct that contains all these values.
  2.3 The event loop must be extracted to a function, and each part of the big if-else clause (server listening, epollin, epollout) has to be also on their own function.
    2.3.1 First move to a function the "Incoming connection on Listening socket clause".
    2.3.2 Then move to a function the if clause which does the EPOLLIN, the one that reads the request.
    2.3.3 Finally move to a function the if clause which does the EPOLLOUT, the one that sends the file to the client. 
3. Fix this bug: for big files, sendfile() does not write everything in one go to the client, meaning we do not need to close immediately the connection. We need to keep track of the offset, feel free to modify existing client datastructure if it helps to track state and close connection only when the whole file was written.
   
