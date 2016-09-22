/*
	SockWebServer, a simple Arduino web/websocket server
	developed by Carlo Benedetti (2016)
   
	based on:
	-	Webduino library originally developed and by Ben Combee, Ran Talbott, 
		Christopher Lee, Martin Lormes, Francisco M Cuenca-Acuna
	-	WebSocketServer 2012 version by Per Ejeklint.
	
	This library integrates onto a single HTTP port (usually 80) both the HTTP
	server and the WEBSOCKET server when the 'upgrade' is requested.
	The concurrent connections that this server is capable to mantain depends
	by the hardware capabilities: W5100 chip shield allows up to 4 clients;
	the W5500 one can support to up to 8 clients.

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/
#define FISHINO_H
//#define ethernet_h
//#define ethernet2_h

//#define DEBUG_WEBSOCKETS(...) Serial.printf( __VA_ARGS__ )
#ifndef DEBUG_WEBSOCKETS
	#define DEBUG_WEBSOCKETS(...)
#endif

#include <string.h>
#include <stdlib.h>
#include <Streaming.h>

#if defined FISHINO_H
	#include <Fishino.h>
	#include <FishinoClient.h>
	#include <FishinoServer.h>
	#define WS_CLIENT	FishinoClient
	#define WS_SERVER	FishinoServer
#elif defined ethernet2_h
	#include <Ethernet2.h>
	#include <EthernetClient.h>
	#include <EthernetServer.h>
	#define WS_CLIENT	EthernetClient
	#define WS_SERVER	EthernetServer
#else	
	#include <Ethernet.h>
	#include <EthernetClient.h>
	#include <EthernetServer.h>
	#define WS_CLIENT	EthernetClient
	#define WS_SERVER	EthernetServer
#endif

#ifndef SOCKWEBSERVER_H_
#define SOCKWEBSERVER_H_

/********************************************************************
 * CONFIGURATION
 ********************************************************************/

#define SOCKWEBSERVER_VERSION 0001
#define SOCKWEBSERVER_VERSION_STRING "0.1"

// standard END-OF-LINE marker in HTTP
#define CRLF "\r\n"

// If processConnection is called without a buffer, it allocates one
// of 32 bytes
#define WEBDUINO_DEFAULT_REQUEST_LENGTH 64

// How long to wait before considering a connection as dead when
// reading the HTTP request.  Used to avoid DOS attacks.
#ifndef WEBDUINO_READ_TIMEOUT_IN_MS
#define WEBDUINO_READ_TIMEOUT_IN_MS 1000
#endif

#ifndef WEBDUINO_COMMANDS_COUNT
#define WEBDUINO_COMMANDS_COUNT 8
#endif

#ifndef WEBDUINO_URL_PATH_COMMAND_LENGTH
#define WEBDUINO_URL_PATH_COMMAND_LENGTH 8
#endif

#ifndef WEBDUINO_FAIL_MESSAGE
#define WEBDUINO_FAIL_MESSAGE "<h1>EPIC FAIL</h1>"
#endif

#ifndef WEBDUINO_AUTH_REALM
#define WEBDUINO_AUTH_REALM "Webduino"
#endif // #ifndef WEBDUINO_AUTH_REALM

#ifndef WEBDUINO_AUTH_MESSAGE
#define WEBDUINO_AUTH_MESSAGE "<h1>401 Unauthorized</h1>"
#endif // #ifndef WEBDUINO_AUTH_MESSAGE

#ifndef WEBDUINO_SERVER_ERROR_MESSAGE
#define WEBDUINO_SERVER_ERROR_MESSAGE "<h1>500 Internal Server Error</h1>"
#endif // WEBDUINO_SERVER_ERROR_MESSAGE

#ifndef WEBDUINO_OUTPUT_BUFFER_SIZE
#define WEBDUINO_OUTPUT_BUFFER_SIZE 32
#endif // WEBDUINO_OUTPUT_BUFFER_SIZE

// declared in wiring.h
extern "C" unsigned long millis(void);

// declare a static string
#ifdef __AVR__
#define P(name)   static const unsigned char name[] __attribute__(( section(".progmem." #name) ))
#else
#define P(name)   static const unsigned char name[]
#endif

// returns the number of elements in the array
#define SIZE(array) (sizeof(array) / sizeof(*array))

#ifdef _VARIANT_ARDUINO_DUE_X_
#define pgm_read_byte(ptr) (unsigned char)(* ptr)
#endif
/********************************************************************
 * DECLARATIONS
 ********************************************************************/

/* Return codes from nextURLparam.  NOTE: URLPARAM_EOS is returned
 * when you call nextURLparam AFTER the last parameter is read.  The
 * last actual parameter gets an "OK" return code. */

enum URLPARAM_RESULT {	URLPARAM_OK,
						URLPARAM_NAME_OFLO,
						URLPARAM_VALUE_OFLO,
						URLPARAM_BOTH_OFLO,
						URLPARAM_EOS         // No params left
				};

class WebSocket;
class WebServer: public Print {
public:
	// passed to a command to indicate what kind of request was received
	enum ConnectionType { INVALID, GET, HEAD, POST, PUT, DELETE, PATCH };

	enum CLIENT_STATUS	{ CONNECTION_FAILED, ALREADY_CONNECTED, SUCCESSFULLY_CONNECTED };
	
	// any commands registered with the web server have to follow
	// this prototype.
	// url_tail contains the part of the URL that wasn't matched against
	//          the registered command table.
	// tail_complete is true if the complete URL fit in url_tail,  false if
	//          part of it was lost because the buffer was too small.
	typedef void Command(WebServer &server, ConnectionType type, char *url_tail, bool tail_complete);

	// Prototype for the optional function which consumes the URL path itself.
	// url_path contains pointers to the seperate parts of the URL path where '/'
	//          was used as the delimiter.
	typedef void UrlPathCommand(WebServer &server, ConnectionType type, char **url_path, char *url_tail, bool tail_complete);

	// constructor for webserver object
	WebServer(const char *urlPrefix = "", uint16_t port = 80, byte maxConnections = 4);

    // Callback functions definition.
    typedef void DataCallback(WebSocket &socket, char *socketString, byte frameLength);
    typedef void Callback(WebSocket &socket);
    
    // Callbacks
    void registerDataCallback(DataCallback *callback);
    void registerConnectCallback(Callback *callback);
    void registerDisconnectCallback(Callback *callback);
 
    // Main listener for incoming data. Should be called from the loop.
    void listen();

    // Connection count
    byte connectionCount();

    // Broadcast to all connected clients.
    void send(char *str, byte length);

	// start listening for connections
	void begin();

	// check for an incoming connection, and if it exists, process it
	// by reading its request and calling the appropriate command
	// handler.  This version is for compatibility with apps written for
	// version 1.1,  and allocates the URL "tail" buffer internally.
	void loop();

	// check for an incoming connection, and if it exists, process it
	// by reading its request and calling the appropriate command
	// handler.  This version saves the "tail" of the URL in buff.
	void processConnection(char *buff, int *bufflen);

	// set command that's run when you access the root of the server
	void setDefaultCommand(Command *cmd);

	// set command run for undefined pages
	void setFailureCommand(Command *cmd);

	// add a new command to be run at the URL specified by verb
	void addCommand(const char *verb, Command *cmd);

	// Set command that's run if default command or URL specified commands do
	// not run, uses extra url_path parameter to allow resolving the URL in the
	// function.
	void setUrlPathCommand(UrlPathCommand *cmd);

	// utility function to output CRLF pair
	void printCRLF();

	// output a string stored in program memory, usually one defined
	// with the P macro
	void printP(const unsigned char *str);

	// inline overload for printP to handle signed char strings
	void printP(const char *str) { printP((unsigned char*)str); }

	// support for C style formating
	void printf(char *fmt, ... );
	#ifdef F
	void printf(const __FlashStringHelper *format, ... );
	#endif

	// output raw data stored in program memory
	void writeP(const unsigned char *data, size_t length);

	// returns next character or -1 if we're at end-of-stream
	int read();

	// put a character that's been read back into the input pool
	void push(int ch);

	// returns true if the string is next in the stream.  Doesn't
	// consume any character if false, so can be used to try out
	// different expected values.
	bool expect(const char *expectedStr);

	// returns true if a number, with possible whitespace in front, was
	// read from the server stream.  number will be set with the new
	// value or 0 if nothing was read.
	bool readInt(int &number);

	// reads a header value, stripped of possible whitespace in front,
	// from the server stream
	void readHeader(char *value, int valueLen);

	// Read the next keyword parameter from the socket.  Assumes that other
	// code has already skipped over the headers,  and the next thing to
	// be read will be the start of a keyword.
	//
	// returns true if we're not at end-of-stream
	bool readPOSTparam(char *name, int nameLen, char *value, int valueLen);

	// Read the next keyword parameter from the buffer filled by getRequest.
	//
	// returns 0 if everything weent okay,  non-zero if not
	// (see the typedef for codes)
	URLPARAM_RESULT nextURLparam(char **tail, char *name, int nameLen,
							   char *value, int valueLen);

	// compare string against credentials in current request
	//
	// authCredentials must be Base64 encoded outside of Webduino
	// (I wanted to be easy on the resources)
	//
	// returns true if strings match, false otherwise
	bool checkCredentials(const char authCredentials[45]);

	// output headers and a message indicating a server error
	void httpFail();

	// output headers and a message indicating "401 Unauthorized"
	void httpUnauthorized();

	// output headers and a message indicating "500 Internal Server Error"
	void httpServerError();

	// output headers indicating "204 No Content" and no further message
	void httpNoContent();

	// output standard headers indicating "200 Success".  You can change the
	// type of the data you're outputting or also add extra headers like
	// "Refresh: 1".  Extra headers should each be terminated with CRLF.
	void httpSuccess(const char *contentType = "text/html; charset=utf-8",
				   const char *extraHeaders = NULL);

	// used with POST to output a redirect to another URL.  This is
	// preferable to outputting HTML from a post because you can then
	// refresh the page without getting a "resubmit form" dialog.
	void httpSeeOther(const char *otherURL);

	// implementation of write used to implement Print interface
	virtual size_t write(uint8_t);
	virtual size_t write(const uint8_t *buffer, size_t size);

	// tells if there is anything to process
	uint8_t available();

	// Flush the send buffer
	void flushBuf(); 

	// Close the current connection and flush ethernet buffers
	void reset();
	
	void setFavicon( char *favicon, size_t flen);
	void setPingTimeout( unsigned long interval,  unsigned long timeout );
	void handlePing();
	
private:

#ifdef FISHINO_H
	FishinoServer m_server;
	FishinoClient m_client;
#else
	EthernetServer m_server;
	EthernetClient m_client;
#endif

    // websockets data
    WebSocket **m_connections;			// Pointer array of clients:
	
	void handleNewClients();
	void handleClientData();
	int getConnectionIndex();
	void deleteConnection( byte idx );	// delete an indexed connection
	

    int m_maxConnections;
    byte m_connectionCount;
	//
	
	char *m_favicon;
	size_t m_favicon_len;
	
	const char *m_urlPrefix;
	unsigned char m_pushback[32];
	unsigned char m_pushbackDepth;
	unsigned long m_ping_interval;		// websocket ping-pong interval in mS
	unsigned long m_ping_timeout;		// Websocket timeout in mS
	unsigned long m_ping_millis;		// websocket current interval value in mS
	bool m_ping_enabled;

	int m_contentLength;
	char m_authCredentials[51];
	bool m_readingContent;
	char m_ws_upgrade[32];
	char m_ws_host[32];
	char m_ws_connection[32];
	int m_ws_version;
	char m_ws_key[32];

	Command *m_failureCmd;
	Command *m_defaultCmd;
	
	struct CommandMap {
		const char *verb;
		Command *cmd;
	} m_commands[WEBDUINO_COMMANDS_COUNT];
	
	unsigned char m_cmdCount;
	UrlPathCommand *m_urlPathCmd;

	uint8_t m_buffer[WEBDUINO_OUTPUT_BUFFER_SIZE];
	uint8_t m_bufFill;

	void getRequest(WebServer::ConnectionType &type, char *request, int *length);
	bool dispatchCommand(ConnectionType requestType, char *verb, bool tail_complete);
	void processHeaders();
	static void defaultFailCmd(WebServer &server, ConnectionType type, char *url_tail, bool tail_complete);
	void noRobots(ConnectionType type);
	void favicon(ConnectionType type);
	
protected:
	friend class WebSocket;
    // Pointer to the callback function the user should provide
    DataCallback *onData;
    Callback *onConnect;
    Callback *onDisconnect;

};

class WebSocket {
    WebServer *m_server;

public:
    // Constructor.
    WebSocket( WebServer *server, WS_CLIENT cli, int idx );

    // Are we connected?
    bool isConnected();
	
    // Embeds data in frame and sends to client.
    bool send(char *str, byte length);

    // Handle incoming data.
    void listen();

	// Return client object
    WS_CLIENT getClient();
	
	// Return client index
    int getClientIndex();

    // Disconnect user gracefully.
    void disconnectStream();
	
	void resetPongMillis();
	unsigned long getPongMillis();

private:
    WS_CLIENT m_client;
	int	m_index;
	
    enum State {DISCONNECTED, CONNECTED} state;

	char *upgrade;
	char *host;
	char *connection;
	char *key;
	int version;
	
	// here is stored the current timer in order to close the connection if no pong
	// is received by any client upon a ping message.
	unsigned long m_pong_timer_millis;
	
    // Discovers if the client's header is requesting an upgrade to a
    // websocket connection.
    bool doHandshake();
    
    // Reads a frame from client. Returns false if user disconnects, 
    // or unhandled frame is received. Server must then disconnect, or an error occurs.
    bool getFrame();
};


#endif // SOCKWEBSERVER_H_
