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
4. Before the event loop, in server.c, there is the configuration of the event fd and the event shared struct and the events allocated memory. Do all of these related event initializations in a function, and return a struct which groups all of them, which will be used in the event loop.
5. Add a .js file and add to the UI a button that when clicked displays the current time, using js logic. The time must update every second. Add also some styling to the button. It must also display the time zone associated.
6. Now the source code has been moved to the src/ folder, and the static content to src/public/. Modify the server.c file accordingly, so the files can be found within src/public/.


### Infrastructure 

1. Use Terraform and generate file(s) using AWS provider to launch a t3 micro in eu-central-1 using the ami linux 2023 image. 
  1.1 Network settings: default vpc, availability zone eu-central-1a, auto-assign public ip.
  1.2 Attach to it a new security group that allows traffic from anywhere. incoming ssh and http on port 80. Require a key pair login, the key already exist and is called frankfurt\_v2.pem. Storage just default configs.


### Ansible

1. Generate an Ansible playbook to manage the t3 instance mentioned in the Infrastructure section of this document and to deploy the source code in the src/ folder.
