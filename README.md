web/websocket server library for arduino/fishino boards.
sources @ branch_01.

This library integrates onto a single HTTP port (usually 80) both the HTTP server and the WEBSOCKET server
when the 'upgrade' is requested. The concurrent connections that this server is capable to mantain depends
on the hardware capabilities: W5100 chip shield allows up to 4 clients; the W5500 one can support to up to 8 clients.

Limitations:
	This version supports single frame messages only. multiframes to come.
	This version supports txt messages only. bins to come.

you need to include these libs in your libraries directory:
	- sha1
	- base64

22-09-2016: added ping-pong opcode handle in order to keep timed-out connections active. this is usefull
	for those arduino cards like fishino serie that drop connections on inactivity timeout.
26-01-2017: bug fixes.