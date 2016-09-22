# SockWebServer
web/websocket server library for arduino/fishino boards

This library integrates onto a single HTTP port (usually 80) both the HTTP
server and the WEBSOCKET server when the 'upgrade' is requested.
The concurrent connections that this server is capable to mantain depends
on the hardware capabilities: W5100 chip shield allows up to 4 clients;
the W5500 one can support to up to 8 clients.

22-09-2016: added ping-pong opcode handle in order to keep timed-out connections active. this is usefull for those arduino cards like fishino serie that drop connections on inactivity timeout.
