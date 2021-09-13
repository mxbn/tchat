## Minimal terminal chat app

Miniature proof of concept.  

* Compile with `make`.  
  * requires `g++`, `make` and `libncurses-dev`.  
* Start `./build/tc-server` on a server machine.  
* Start `./build/tc-client` on a client machine with the server's IP address and the chat user name passed as arguments.  
  * `127.0.0.1` and environment variable `$USER` are default parameters.  
