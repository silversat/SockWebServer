/*
	Websocket-Arduino, a simple (working!) websocket implementation for Arduino
	Copyright 2016 Karl Benny
	
	based on a 2012 version by Per Ejeklint
*/
#include <Arduino.h> 			// Arduino 1.0 or greater is required
#include <stdlib.h>
#include <SPI.h>

	#include <Fishino.h>
	#include <FishinoServer.h>
	#include <FishinoClient.h>
	#define WS_CLIENT	FishinoClient
	#define WS_SERVER	FishinoServer
/*
#if defined FISHINO_H
	#include <Fishino.h>
	#define WS_CLIENT	FishinoClient
	#define WS_SERVER	FishinoServer
#elif defined ethernet2_h
	#include <Ethernet2.h>
	#define WS_CLIENT	EthernetClient
	#define WS_SERVER	EthernetServer
#elif defined ethernet_h
	#include <Ethernet.h>
	#define WS_CLIENT	EthernetClient
	#define WS_SERVER	EthernetServer
#else
	#warning NO NET LIBRARY SELECTED
#endif
*/
#ifndef WEBSOCKET_H_
#define WEBSOCKET_H_

#define CRLF "\r\n"				// CRLF characters to terminate lines/handshakes in headers.

class WebSocket;
class WebSocketServer {
public:
    // Constructor.
    WebSocketServer(const char *urlPrefix = "/", int inPort = 80, byte maxConnections = 4);
    
    // Callback functions definition.
    typedef void DataCallback(WebSocket &socket, char *socketString, byte frameLength);
    typedef void Callback(WebSocket &socket);
    
    // Callbacks
    void registerDataCallback(DataCallback *callback);
    void registerConnectCallback(Callback *callback);
    void registerDisconnectCallback(Callback *callback);
    
    // Start listening for connections.
    void begin();
    
    // Main listener for incoming data. Should be called from the loop.
    void listen();

    // Connection count
    byte connectionCount();

    // Broadcast to all connected clients.
    void send(char *str, byte length);

private:
	void handleNewClients();
	void handleClientData();
 
    WS_SERVER m_server;

    const char *m_socket_urlPrefix;
    int m_maxConnections;

    byte m_connectionCount;

    // Pointer array of clients:
    WebSocket **m_connections;

protected:
friend class WebSocket;
    // Pointer to the callback function the user should provide
    DataCallback *onData;
    Callback *onConnect;
    Callback *onDisconnect;
};

class WebSocket {
    WebSocketServer *m_server;

public:
    // Constructor.
    WebSocket(WebSocketServer *server, WS_CLIENT cli);

    // Are we connected?
    bool isConnected();
	
    // Embeds data in frame and sends to client.
    bool send(char *str, byte length);

    // Handle incoming data.
    void listen();

	// Return client object
    WS_CLIENT getClient();

private:
    WS_CLIENT client;
    enum State {DISCONNECTED, CONNECTED} state;

    // Discovers if the client's header is requesting an upgrade to a
    // websocket connection.
    bool doHandshake();
    
    // Reads a frame from client. Returns false if user disconnects, 
    // or unhandled frame is received. Server must then disconnect, or an error occurs.
    bool getFrame();

    // Disconnect user gracefully.
    void disconnectStream();
};

#endif
