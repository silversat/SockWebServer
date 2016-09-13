# SockWebServer
web/websocket server library for arduino/fishino boards

This library integrates onto a single HTTP port (usually 80) both the HTTP
server and the WEBSOCKET server when the 'upgrade' is requested.
The concurrent connections that this server is capable to mantain depends
on the hardware capabilities: W5100 chip shield allows up to 4 clients;
the W5500 one can support to up to 8 clients.
