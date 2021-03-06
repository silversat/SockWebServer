#include <SockWebServer.h>
#include <sha1.h>
#include <Base64.h>

#define MASK_LEN	4
#define DATA_LEN	64
#define NO_NEW		-1
#define NOT_PRESENT	-1

struct Frame {
    bool isMasked;
    bool isFinal;
    byte opcode;
    byte mask[MASK_LEN];
    byte length;
    char data[DATA_LEN];
} frame;

//===============================================================================
//								WEBSERVER CLASS
//===============================================================================

WebServer::WebServer(const char *urlPrefix, uint16_t port, byte maxConnections) :
	m_server(port),
	m_client(),
	m_urlPrefix(urlPrefix),
	m_favicon(NULL),
	m_favicon_len(0),
	m_pushbackDepth(0),
	m_contentLength(0),
	m_failureCmd(&defaultFailCmd),
	m_defaultCmd(&defaultFailCmd),
	m_cmdCount(0),
	m_urlPathCmd(NULL),
	m_bufFill(0),
	m_maxConnections(maxConnections),		// websockets max connections
	m_connectionCount(0),					// websockets connections counter
	m_ping_enabled(false)
{
    m_connections = new WebSocket*[m_maxConnections];
	for(uint8_t idx = 0; idx < m_maxConnections; idx++) {
        m_connections[idx] = NULL;
	}
	onConnect = NULL;
    onData = NULL;
    onDisconnect = NULL;
    onPing = NULL;
    onPong = NULL;
}

void WebServer::begin() {
	m_server.begin();
}

void WebServer::loop() {
	char request[WEBDUINO_DEFAULT_REQUEST_LENGTH];
	int  request_len = WEBDUINO_DEFAULT_REQUEST_LENGTH;

	handlePing();
	handleClientData();
	processConnection(request, &request_len);
}

void WebServer::processConnection( char *buff, int *bufflen ) {
	m_client = m_server.available();				// query if a client is waiting for something
	if(m_client) {
		int idx = getConnectionIndex(m_client);		// if yes, check if the client connection is already registered
		if(idx == NOT_PRESENT) {
			m_readingContent = false;
			buff[0] = 0x00;
			ConnectionType requestType = getRequest(buff, bufflen);
			DEBUG_WEBSOCKETS(F("\n*** requestType: %d, request: '%s', len: %d ***\n"), (int)requestType, buff, strlen(buff));

			// don't even look further at invalid requests.
			// this is done to prevent Webduino from hanging
			// - when there are illegal requests,
			// - when someone contacts it through telnet rather than proper HTTP,
			// - etc.
			if(requestType != INVALID) {
				processHeaders();
				DEBUG_WEBSOCKETS(F("*** headers complete ***\n"));
				if (strcmp(buff, "/robots.txt") == 0) {
					noRobots(requestType);
				} else if (strcmp(buff, "/favicon.ico") == 0) {
					favicon(requestType);
				} else if(strcasecmp(m_ws_upgrade, "websocket") == 0) {
					DEBUG_WEBSOCKETS(F("*** upgrading HTTP connection to WEBSOCKET ***\n"));
					idx = handleNewClients();		// return new ws session index if ok.
				}
			}
			int urlPrefixLen = strlen(m_urlPrefix);
			if(idx == NOT_PRESENT) {				// if no new websocket session has been initiated
				if(requestType == INVALID || strncmp(buff, m_urlPrefix, urlPrefixLen) != 0) {
					m_failureCmd(*this, requestType, buff, (*bufflen) >= 0);
				} else if(!dispatchCommand(requestType, buff + urlPrefixLen, (*bufflen) >= 0)) {
					m_failureCmd(*this, requestType, buff, (*bufflen) >= 0);
				}
				flushBuf();							// close the connection
				DEBUG_WEBSOCKETS(F("*** stopping HTTP connection ***\n"));
				reset();
			}
		}
		#ifdef DEBUG_WEBSOCKETS
			ShowSockStatus();
		#endif
	}
}

int WebServer::getConnectionIndex(WS_CLIENT client) {
	int ret = NOT_PRESENT;
	for(int idx = 0; idx < m_maxConnections; idx++) {
		if(m_connections[idx] != NULL) {
			if(m_connections[idx]->isConnected()) {
				if(m_connections[idx]->getClient() == client) {
					ret = m_connections[idx]->getClientIndex();	//  ret = idx ???
					break;
				}
			}
		}
	}
	return ret;
}

int WebServer::getSocketCount( char *protocol ) {
	int ret = 0;
	if(strlen(protocol) > 0) {
		for(int idx = 0; idx < m_maxConnections; idx++) {
			if(m_connections[idx] != NULL) {
				if(strcmp(m_connections[idx]->getProtocol(), protocol) == 0) {
					ret++;
				}
			}
		}
	}
	return ret;
}

int WebServer::handleNewClients() {
/*	for(int idx = 0; idx < m_maxConnections; idx++) {		// check if new client is already recorded
		if(m_connections[idx] != NULL) {
			if(m_connections[idx]->getClient() == m_client) {
				DEBUG_WEBSOCKETS(F("Websocket client %d already connected.\n"), idx);
				return NO_NEW;
			}
		}
	}
*/	for(int idx = 0; idx < m_maxConnections; idx++) {		// It's a new client: Find a slot:
        if(m_connections[idx] != NULL) {
            continue;
		}
		m_connections[idx] = new WebSocket(this, m_client, idx, m_ws_protocol);
        m_connectionCount++;
		DEBUG_WEBSOCKETS(F("Websocket client %d connected.\n"), idx);
		return idx;
    }
    // No room!
	DEBUG_WEBSOCKETS(F("Cannot accept new websocket client, maxConnections reached!\n"));
	return NO_NEW;
}

void WebServer::handleClientData() {
	for(byte idx = 0; idx < m_maxConnections; idx++) {			// First check existing connections:
		if(m_connections[idx] != NULL) {
			if(m_connections[idx]->isConnected()) {
				if(m_ping_enabled) {
					unsigned long sockMillis = m_connections[idx]->getPongMillis();
					if(sockMillis > m_ping_millis and (sockMillis - m_ping_millis) > m_ping_timeout) {
						DEBUG_WEBSOCKETS(F("---> sockMillis: %lu, m_ping_millis: %lu, diff: %lu\n"), sockMillis, m_ping_millis, (sockMillis-m_ping_millis));						
						m_connections[idx]->disconnectStream();
						deleteConnection(idx);
						continue;
					}
				}
				m_connections[idx]->listen();
			} else {
				deleteConnection(idx);
				continue;
			}
		}
	}
}

void WebServer::handlePing() {
	if(m_ping_enabled) {
		if(millis() - m_ping_millis > m_ping_interval) {	
			m_ping_millis = millis();
			if(m_connectionCount > 0) {
				m_server.write((uint8_t) 0x89);		// PING frame opcode
				m_server.write((uint8_t) 0x00);
				if( onPing ) {
					onPing();
				}
				DEBUG_WEBSOCKETS(F("global PING sent @ %lu\n"), millis());
			}
		}
	}
}

void WebServer::deleteConnection( byte idx ) {
	m_connectionCount--;
	delete m_connections[idx];
	m_connections[idx] = NULL;
	DEBUG_WEBSOCKETS(F("client %d has been deleted\n"), idx);
}

void WebServer::flushBuf() {
  if(m_bufFill > 0) {
    m_client.write(m_buffer, m_bufFill);
    m_bufFill = 0;
  }
}

void WebServer::reset() {
	m_pushbackDepth = 0;
	m_client.flush();
	m_client.stop();
}

//-------------------------------------------------------------------
//							WEB COMMANDS
//-------------------------------------------------------------------

void WebServer::setDefaultCommand(Command *cmd) {
	m_defaultCmd = cmd;
}

void WebServer::setFailureCommand(Command *cmd) {
	m_failureCmd = cmd;
}

void WebServer::addCommand(const char *verb, Command *cmd) {
	if(m_cmdCount < SIZE(m_commands)) {
		m_commands[m_cmdCount].verb = verb;
		m_commands[m_cmdCount++].cmd = cmd;
	}
}

void WebServer::setUrlPathCommand(UrlPathCommand *cmd) {
	m_urlPathCmd = cmd;
}

size_t WebServer::write(uint8_t ch) {
	m_buffer[m_bufFill++] = ch;
	if(m_bufFill == sizeof(m_buffer)) {
		m_client.write(m_buffer, sizeof(m_buffer));
		m_bufFill = 0;
	}
	return sizeof(ch);
}

size_t WebServer::write(const uint8_t *buffer, size_t size) {
	flushBuf();		//Flush any buffered output
	return m_client.write(buffer, size);
}

void WebServer::writeP(const unsigned char *data, size_t length) {
	// copy data out of program memory into local storage
	while(length--) {
		write(pgm_read_byte(data++));
	}
}

void WebServer::printP(const unsigned char *str) {
	// copy data out of program memory into local storage
	while(uint8_t value = pgm_read_byte(str++)) {
		write(value);
	}
}

void WebServer::printCRLF() {
	print(CRLF);
}

void WebServer::printf(char *fmt, ... ) {
	char tmp[128]; // resulting string limited to 128 chars
	va_list args;
	va_start (args, fmt);
	vsnprintf(tmp, 128, fmt, args);
	va_end (args);
	print(tmp);
}

#ifdef F
void WebServer::printf(const __FlashStringHelper *format, ... ) {
	char buf[128]; // resulting string limited to 128 chars
	va_list ap;
	va_start(ap, format);
#ifdef __AVR__
	vsnprintf_P(buf, sizeof(buf), (const char *)format, ap); // progmem for AVR
#else
	vsnprintf(buf, sizeof(buf), (const char *)format, ap); // for the rest of the world
#endif  
	va_end(ap);
	print(buf);
}
#endif

bool WebServer::dispatchCommand(ConnectionType requestType, char *verb, bool tail_complete) {
	// if there is no URL, i.e. we have a prefix and it's requested without a
	// trailing slash or if the URL is just the slash
	if((verb[0] == 0) || ((verb[0] == '/') && (verb[1] == 0))) {
		m_defaultCmd(*this, requestType, (char*)"", tail_complete);
		return true;
	}
	// if the URL is just a slash followed by a question mark
	// we're looking at the default command with GET parameters passed
	if((verb[0] == '/') && (verb[1] == '?')) {
		verb+=2; // skip over the "/?" part of the url
		m_defaultCmd(*this, requestType, verb, tail_complete);
		return true;
	}
	// We now know that the URL contains at least one character.  And,
	// if the first character is a slash,  there's more after it.
	if(verb[0] == '/') {
		uint8_t i;
		char *qm_loc;
		uint16_t verb_len;
		uint8_t qm_offset;
		// Skip over the leading "/",  because it makes the code more
		// efficient and easier to understand.
		verb++;
		// Look for a "?" separating the filename part of the URL from the
		// parameters.  If it's not there, compare to the whole URL.
		qm_loc = strchr(verb, '?');
		verb_len = (qm_loc == NULL) ? strlen(verb) : (qm_loc - verb);
		qm_offset = (qm_loc == NULL) ? 0 : 1;
		for(i = 0; i < m_cmdCount; ++i) {
			if((verb_len == strlen(m_commands[i].verb)) && (strncmp(verb, m_commands[i].verb, verb_len) == 0)) {
				// Skip over the "verb" part of the URL (and the question
				// mark, if present) when passing it to the "action" routine
				m_commands[i].cmd(*this, requestType, verb + verb_len + qm_offset, tail_complete);
				return true;
			}
		}
		// Check if UrlPathCommand is assigned.
		if(m_urlPathCmd != NULL) {
			// Initialize with null bytes, so number of parts can be determined.
			char *url_path[WEBDUINO_URL_PATH_COMMAND_LENGTH] = {0};
			uint8_t part = 0;
	
			// URL path should be terminated with null byte.
			*(verb + verb_len) = 0;

			// First URL path part is at the start of verb.
			url_path[part++] = verb;
			// Replace all slashes ('/') with a null byte so every part of the URL
			// path is a seperate string. Add every char following a '/' as a new
			// part of the URL, even if that char is a '/' (which will be replaced
			// with a null byte).
			for(char * p = verb; p < verb + verb_len; p++) {
				if(*p == '/') {
					*p = 0;
					url_path[part++] = p + 1;
					// Don't try to assign out of array bounds.
					if(part == WEBDUINO_URL_PATH_COMMAND_LENGTH) {
						break;
					}
				}
			}
			m_urlPathCmd(*this, requestType, url_path, verb + verb_len + qm_offset, tail_complete);
			return true;
		}
	}
	return false;
}

bool WebServer::checkCredentials(const char authCredentials[45]) {
	return ((strncmp(m_authCredentials, "Basic ", 6) == 0) && (strcmp(authCredentials, m_authCredentials + 6) == 0));
}

void WebServer::httpFail() {
	printP(failMsg1);
#ifndef WEBDUINO_SUPRESS_SERVER_HEADER
	printP(webServerHeader);
#endif
	printP(failMsg2);
}

void WebServer::defaultFailCmd(WebServer &server, WebServer::ConnectionType type, char *url_tail, bool tail_complete) {
	server.httpFail();
}

void WebServer::noRobots(ConnectionType type) {
	httpSuccess("text/plain");
	if(type != HEAD) {
		printP(allowNoneMsg);
	}
}

void WebServer::favicon(ConnectionType type) {
	httpSuccess("image/x-icon","Cache-Control: max-age=31536000\r\n");
	if(type != HEAD) {
		if(m_favicon_len > 0) {
			flushBuf(); //Flush any buffered output
			m_client.write(m_favicon, m_favicon_len);
			DEBUG_WEBSOCKETS(F("*** favicon sent: %d bytes ***\n"), m_favicon_len);
		}
	}
}

void WebServer::httpUnauthorized() {
  printP(unauthMsg1);
#ifndef WEBDUINO_SUPRESS_SERVER_HEADER
  printP(webServerHeader);
#endif
  printP(unauthMsg2);
}

void WebServer::httpServerError() {
  printP(servErrMsg1);
#ifndef WEBDUINO_SUPRESS_SERVER_HEADER
  printP(webServerHeader);
#endif
  printP(servErrMsg2);
}

void WebServer::httpNoContent() {
  printP(noContentMsg1);
#ifndef WEBDUINO_SUPRESS_SERVER_HEADER
  printP(webServerHeader);
#endif
  printP(noContentMsg2);
}

void WebServer::httpSuccess(const char *contentType, const char *extraHeaders) {
	printP(successMsg1);
#ifndef WEBDUINO_SUPRESS_SERVER_HEADER
	printP(webServerHeader);
#endif
	printP(successMsg2);
	print(contentType);
	printCRLF();
	if(extraHeaders) {
		print(extraHeaders);
	}
	printCRLF();
}

void WebServer::httpSeeOther(const char *otherURL) {
	printP(seeOtherMsg1);
#ifndef WEBDUINO_SUPRESS_SERVER_HEADER
	printP(webServerHeader);
#endif
	printP(seeOtherMsg2);
	print(otherURL);
	printCRLF();
	printCRLF();
}

int WebServer::read() {
	if(!m_client) {
		return -1;
	}
	if(m_pushbackDepth == 0) {
		unsigned long timeoutTime = millis() + WEBDUINO_READ_TIMEOUT_IN_MS;

		while(m_client.connected()) {
			// stop reading the socket early if we get to content-length
			// characters in the POST.  This is because some clients leave
			// the socket open because they assume HTTP keep-alive.
			if(m_readingContent) {
				if (m_contentLength == 0) {
					DEBUG_WEBSOCKETS(F("\n*** End of content, terminating connection\n"));
					return -1;
				}
			}

			int ch = m_client.read();

			// if we get a character, return it, otherwise continue in while
			// loop, checking connection status
			if(ch != -1) {
				// count character against content-length
				if(m_readingContent) {
					--m_contentLength;
				}
				return ch;
			} else {
				unsigned long now = millis();
				if(now > timeoutTime) {
					// connection timed out, destroy client, return EOF
					DEBUG_WEBSOCKETS(F("*** Connection timed out\n"));
					reset();
					return -1;
				}
			}
		}

		// connection lost, return EOF
		DEBUG_WEBSOCKETS(F("*** Connection lost\n"));
		return -1;
	} else {
		return m_pushback[--m_pushbackDepth];
	}
}

void WebServer::push(int ch) {
	if(ch == -1) {			// don't allow pushing EOF
		return;
	}
	m_pushback[m_pushbackDepth++] = ch;
	// can't raise error here, so just replace last char over and over
	if(m_pushbackDepth == SIZE(m_pushback)) {
		m_pushbackDepth = SIZE(m_pushback) - 1;
	}
}

bool WebServer::expect(const char *str) {
	const char *curr = str;
	while(*curr != 0) {
		int ch = read();
		if(ch != *curr++) {
			push(ch);			// push back ch and the characters we accepted
			while(--curr != str) {
				push(curr[-1]);
			}
			return false;
		}
	}
	return true;
}

bool WebServer::readInt(int &number) {
	bool negate = false;
	bool gotNumber = false;
	int ch;
	number = 0;

	do {								// absorb whitespace
		ch = read();
	} while (ch == ' ' || ch == '\t');

	if(ch == '-') {						// check for leading minus sign
		negate = true;
		ch = read();
	}

	while(ch >= '0' && ch <= '9') {		// read digits to update number, exit when we find non-digit
		gotNumber = true;
		number = number * 10 + ch - '0';
		ch = read();
	}
	push(ch);
	if(negate) {
		number = -number;
	}
	return gotNumber;
}

bool WebServer::readPOSTparam(char *name, int nameLen, char *value, int valueLen) {
	// assume name is at current place in stream
	int ch;
	// to not to miss the last parameter
	bool foundSomething = false;

	// clear out name and value so they'll be NUL terminated
	memset(name, 0, nameLen);
	memset(value, 0, valueLen);

	// decrement length so we don't write into NUL terminator
	--nameLen;
	--valueLen;

	while((ch = read()) != -1) {
		foundSomething = true;
		if (ch == '+') {
			ch = ' ';
		} else if (ch == '=') {
			/* that's end of name, so switch to storing in value */
			nameLen = 0;
			continue;
		} else if (ch == '&') {
			/* that's end of pair, go away */
			return true;
		} else if (ch == '%') {
			/* handle URL encoded characters by converting back to original form */
			int ch1 = read();
			int ch2 = read();
			if (ch1 == -1 || ch2 == -1) {
				return false;
			}
			char hex[3] = { (char)ch1, (char)ch2, '\0' };
			ch = strtoul(hex, NULL, 16);
		}
		// output the new character into the appropriate buffer or drop it if
		// there's no room in either one.  This code will malfunction in the
		// case where the parameter name is too long to fit into the name buffer,
		// but in that case, it will just overflow into the value buffer so
		// there's no harm.
		if (nameLen > 0) {
		  *name++ = ch;
		  --nameLen;
		} else if (valueLen > 0) {
		  *value++ = ch;
		  --valueLen;
		}
	}
	if(foundSomething) {
		// if we get here, we have one last parameter to serve
		return true;
	} else {
		// if we get here, we hit the end-of-file, so POST is over and there
		// are no more parameters
		return false;
	}
}

/* Retrieve a parameter that was encoded as part of the URL, stored in
 * the buffer pointed to by *tail.  tail is updated to point just past
 * the last character read from the buffer. */
URLPARAM_RESULT WebServer::nextURLparam(char **tail, char *name, int nameLen, char *value, int valueLen) {
	// assume name is at current place in stream
	char ch, hex[3];
	URLPARAM_RESULT result = URLPARAM_OK;
	char *s = *tail;
	bool keep_scanning = true;
	bool need_value = true;

	// clear out name and value so they'll be NUL terminated
	memset(name, 0, nameLen);
	memset(value, 0, valueLen);

	if(*s == 0) {
		return URLPARAM_EOS;
	}
	// Read the keyword name
	while (keep_scanning) {
		ch = *s++;
		switch (ch) {
			case 0:
				s--;  // Back up to point to terminating NUL Fall through to "stop the scan" code
			case '&':
				/* that's end of pair, go away */
				keep_scanning = false;
				need_value = false;
				break;
			case '+':
				ch = ' ';
				break;
			case '%':
				/* handle URL encoded characters by converting back to original form */
				if((hex[0] = *s++) == 0) {
					s--;        // Back up to NUL
					keep_scanning = false;
					need_value = false;
				} else {
					if((hex[1] = *s++) == 0) {
						s--;  // Back up to NUL
						keep_scanning = false;
						need_value = false;
					} else {
						hex[2] = 0;
						ch = strtoul(hex, NULL, 16);
					}
				}
				break;
			case '=':
				/* that's end of name, so switch to storing in value */
				keep_scanning = false;
				break;
		}

		// check against 1 so we don't overwrite the final NUL
		if(keep_scanning && (nameLen > 1)) {
			*name++ = ch;
			--nameLen;
		} else if(keep_scanning) {
			result = URLPARAM_NAME_OFLO;
		}
	}

	if(need_value && (*s != 0)) {
		keep_scanning = true;
		while(keep_scanning) {
			ch = *s++;
			switch (ch) {
				case 0:
					s--;  // Back up to point to terminating NUL Fall through to "stop the scan" code
				case '&':
					/* that's end of pair, go away */
					keep_scanning = false;
					need_value = false;
					break;
				case '+':
					ch = ' ';
					break;
				case '%':
					/* handle URL encoded characters by converting back to original form */
					if((hex[0] = *s++) == 0) {
						s--;  // Back up to NUL
						keep_scanning = false;
						need_value = false;
					} else {
						if ((hex[1] = *s++) == 0) {
							s--;  // Back up to NUL
							keep_scanning = false;
							need_value = false;
						} else {
							hex[2] = 0;
							ch = strtoul(hex, NULL, 16);
						}
					}
					break;
			}

			// check against 1 so we don't overwrite the final NUL
			if(keep_scanning && (valueLen > 1)) {
				*value++ = ch;
				--valueLen;
			} else if(keep_scanning) {
				result = (result == URLPARAM_OK) ? URLPARAM_VALUE_OFLO : URLPARAM_BOTH_OFLO;
			}
		}
	}
	*tail = s;
	return result;
}

/* Read and parse the first line of the request header.
   The "command" (GET/HEAD/POST) is translated into a numeric value in type.
   The URL is stored in request,  up to the length passed in length
   NOTE 1: length must include one byte for the terminating NUL.
   NOTE 2: request is NOT checked for NULL,  nor length for a value < 1.
   Reading stops when the code encounters a space, CR, or LF.  If the HTTP
   version was supplied by the client, it will still be waiting in the input
   stream when we exit.
  
   On return, length contains the amount of space left in request. If it's
   less than 0, the URL was longer than the buffer, and part of it had to
   be discarded. */
WebServer::ConnectionType WebServer::getRequest( char *request, int *length ) {
	WebServer::ConnectionType type = INVALID;
	--*length; // save room for NUL

	// store the HTTP method line of the request
	if(expect("GET ")) {
		type = GET;
	} else if(expect("HEAD ")) {
		type = HEAD;
	} else if(expect("POST ")) {
		type = POST;
	} else if(expect("PUT ")) {
		type = PUT;
	} else if(expect("DELETE ")) {
		type = DELETE;
	} else if(expect("PATCH ")) {
		type = PATCH;
	} else {					// if it doesn't start with any of those, we have an unknown method
		return type;			// so just get out of here
	}
	int ch;
	while((ch = read()) != -1) {
		if(ch == ' ' || ch == '\n' || ch == '\r') {		// stop storing at first space or end of line
			break;
		}
		if(*length > 0) {
			*request = ch;
			++request;
		}
		--*length;
	}
	*request = 0x00;		// NUL terminate
	return type;
}

void WebServer::processHeaders() {
	// look for three things: the Content-Length header, the Authorization
	// header, and the double-CRLF that ends the headers.
	
	// empty the m_authCredentials before every run of this function.
	// otherwise users who don't send an Authorization header would be treated
	// like the last user who tried to authenticate (possibly successful)
	m_authCredentials[0] = 0x00;
	// and the others variables too...
	m_ws_upgrade[0] = 0x00;
	m_ws_host[0] = 0x00;
	m_ws_connection[0] = 0x00;
	m_ws_version = 0;
	m_ws_protocol[0] = 0x00;
	m_ws_key[0] = 0x00;
	m_contentLength = 0;
	m_ws_version = 0;

	while(true) {
		if(expect("Content-Length:")) {
			readInt(m_contentLength);
			DEBUG_WEBSOCKETS(F("*** got Content-Length %d ***\n"), m_contentLength);
			continue;
		}
		if(expect("Authorization:")) {
			readHeader(m_authCredentials, sizeof(m_authCredentials));
			DEBUG_WEBSOCKETS(F("*** got Authorization: %s, len: %d of %d ***\n"), m_authCredentials, strlen(m_authCredentials), sizeof(m_authCredentials));
			continue;
		}
		if(expect("Upgrade:")) {
			readHeader(m_ws_upgrade, sizeof(m_ws_upgrade));
			DEBUG_WEBSOCKETS(F("*** got Upgrade: %s, len: %d of %d ***\n"), m_ws_upgrade, strlen(m_ws_upgrade), sizeof(m_ws_upgrade));
			continue;
		}
		if(expect("Host:")) {
			readHeader(m_ws_host, sizeof(m_ws_host));
			DEBUG_WEBSOCKETS(F("*** got Host: %s, len: %d of %d ***\n"), m_ws_host, strlen(m_ws_host), sizeof(m_ws_host));
			continue;
		}
		if(expect("Connection:")) {
			readHeader(m_ws_connection, sizeof(m_ws_connection));
			DEBUG_WEBSOCKETS(F("*** got Connection: %s, len: %d of %d ***\n"), m_ws_connection, strlen(m_ws_connection), sizeof(m_ws_connection));
			continue;
		}
		if(expect("Sec-WebSocket-Version:")) {
			readInt(m_ws_version);
			DEBUG_WEBSOCKETS(F("*** got Sec-WebSocket-Version: %d ***\n"), m_ws_version);
			continue;
		}
		if(expect("Sec-WebSocket-Protocol:")) {
			readHeader(m_ws_protocol, sizeof(m_ws_protocol));
			DEBUG_WEBSOCKETS(F("*** got Sec-WebSocket-Protocol: %s, len: %d of %d ***\n"), m_ws_protocol, strlen(m_ws_protocol), sizeof(m_ws_protocol));
			continue;
		}
		if(expect("Sec-WebSocket-Key:")) {
			readHeader(m_ws_key, sizeof(m_ws_key));
			DEBUG_WEBSOCKETS(F("*** got Sec-WebSocket-Key: %s, len: %d of %d ***\n"), m_ws_key, strlen(m_ws_key), sizeof(m_ws_key));
			continue;
		}
		if(expect(CRLF CRLF)) {
			m_readingContent = true;
			return;
		}
		// no expect checks hit, so just absorb a character and try again
		if(read() == -1) {
			return;
		}
	}
}

void WebServer::readHeader(char *value, int valueLen) {
	int ch;
	memset(value, 0, valueLen);
	--valueLen;

	// absorb whitespace
	do {
		ch = read();
	} while (ch == ' ' || ch == '\t');

	// read rest of line
	do {
		if (valueLen > 1) {
			*value++=ch;
			--valueLen;
		}
		ch = read();
	} while (ch != '\r');
	push(ch);
}

uint8_t WebServer::available() {
  return m_server.available();
}

void WebServer::send( char *data, byte length ) {
	m_server.write((uint8_t) 0x81); 			// Txt frame opcode
	m_server.write((uint8_t) length); 			// Length of data
	m_server.write((const uint8_t *)data, length);
}

byte WebServer::connectionCount() {
    return m_connectionCount;
}

void WebServer::setFavicon( char *favicon, size_t flen ) {
	m_favicon_len = flen;
	m_favicon = new char[m_favicon_len];
	memcpy(m_favicon, favicon, m_favicon_len);
}

void WebServer::setPingTimeout(unsigned long interval,  unsigned long timeout) {
	m_ping_enabled = (interval > 0 and timeout > 0);
	if(m_ping_enabled) {
		m_ping_interval = interval;
		m_ping_timeout = timeout;
		m_ping_millis = millis();
	}
}

//-------------------------------------------------------------------
//					CALL-BACK REGISTER FUNCTIONS
//-------------------------------------------------------------------

void WebServer::registerConnectCallback(Callback *callback) {
    onConnect = callback;
}

void WebServer::registerDataCallback(DataCallback *callback) {
    onData = callback;
}

void WebServer::registerDisconnectCallback(Callback *callback) {
    onDisconnect = callback;
}

void WebServer::registerPingCallback(VoidCallback *callback) {
    onPing = callback;
}

void WebServer::registerPongCallback(Callback *callback) {
    onPong = callback;
}

//-------------------------------------------------------------------
//					SERVICE AND TEST FUNCTIONS
//-------------------------------------------------------------------

void WebServer::ShowSockStatus() {
	#ifdef W5xxx
		Serial.println(F("-----------------------------------------"));
		for(int i = 0; i < MAX_SOCK_NUM; i++) {
			Serial.printf(F("Socket#%d:"), i);
			switch(W5xxx.readSnSR(i)) {
				case 0x00:
					Serial.print(F("CLOSED     "));
					break;
				case 0x13:
					Serial.print(F("INIT       "));
					break;
				case 0x14:
					Serial.print(F("LISTEN     "));
					break;
				case 0x15:
					Serial.print(F("SYNSENT    "));
					break;
				case 0x16:
					Serial.print(F("SYNRECV    "));
					break;
				case 0x17:
					Serial.print(F("ESTABLISHED"));
					break;
				case 0x18:
					Serial.print(F("FIN_WAIT   "));
					break;
				case 0x1A:
					Serial.print(F("CLOSING    "));
					break;
				case 0x1B:
					Serial.print(F("TIME_WAIT  "));
					break;
				case 0x1C:
					Serial.print(F("CLOSE_WAIT "));
					break;
				case 0x1D:
					Serial.print(F("LAST_ACK   "));
					break;
				case 0x22:
					Serial.print(F("UDP        "));
					break;
				case 0x32:
					Serial.print(F("IPRAW      "));
					break;
				case 0x42:
					Serial.print(F("MACRAW     "));
					break;
				case 0x5F:
					Serial.print(F("PPPOE      "));
					break;
				default:
					Serial.printf(F("0x%02X"), W5xxx.readSnSR(i));
			}
			Serial.printf(F(" %4d D:"), W5xxx.readSnPORT(i));

			uint8_t dip[4];
			W5xxx.readSnDIPR(i, dip);
			for(int j = 0; j < 4; j++) {
				Serial.print(dip[j],10);
				if (j<3) Serial.print(".");
			}
			Serial.print(" (");
			Serial.print(W5xxx.readSnDPORT(i));
			Serial.println(")");
		}
		Serial.println(F("-----------------------------------------"));
	#endif
}

//===============================================================================
//
//								WEBSOCKETS CLASS
//
//===============================================================================
WebSocket::WebSocket( WebServer* server, WS_CLIENT cli, int idx, char* protocol ) :
    m_server(server),
    m_client(cli),
	m_index(idx),
	m_pong_timer_millis(0),
	m_protocol()
{
	if(strlen(protocol) < sizeof(m_protocol)) {
		strcpy(m_protocol, protocol);
	} else {
		memset(m_protocol, 0, sizeof(m_protocol));
		strncpy(m_protocol, protocol, sizeof(m_protocol)-1);
	}
    if(doHandshake()) {
        state = CONNECTED;
        if(m_server->onConnect) {
            m_server->onConnect(*this);
		}
    } else {
		disconnectStream();
	}
}

bool WebSocket::isConnected() {
    return (state == CONNECTED);
}

void WebSocket::resetPongMillis() {
	if( m_server->onPong ) {
		m_server->onPong(*this);
	}
	m_pong_timer_millis = millis();
}
	
unsigned long WebSocket::getPongMillis() {
	return m_pong_timer_millis;
}
	
void WebSocket::listen() {
	if(m_client) {
		if(m_client.available()) {
			if(!getFrame()) {
				disconnectStream();
			}
		}
	} else {
		disconnectStream();
	}
}

WS_CLIENT WebSocket::getClient() {
	return m_client;
}

char* WebSocket::getProtocol() {
	return m_protocol;
}

int WebSocket::getClientIndex() {
	return m_index;
}

void WebSocket::disconnectStream() {
	DEBUG_WEBSOCKETS(F("Disconnecting client %d\n"), m_index);
    state = DISCONNECTED;
    if( m_server->onDisconnect ) {
        m_server->onDisconnect(*this);
	}
    m_client.flush();
    delay(1);
    m_client.stop();
}
/*
void WebSocket::disconnectStream() {
	DEBUG_WEBSOCKETS(F("Disconnecting client %d\n"), m_index);
	while(m_client.connected()) {
		m_client.flush();
		m_client.stop();
		delay(10);				// original value: 1
	}
	m_client = NULL;
    state = DISCONNECTED;
    if( m_server->onDisconnect ) {
        m_server->onDisconnect(*this);
	}
}
*/
bool WebSocket::doHandshake() {
	bool ret = false;
    char temp[64];
    char key[80];
	char upgrade[32];
	char host[32];
	char connection[32];
	int version;

	version = m_server->m_ws_version;
	strcpy(host, m_server->m_ws_host);
	strcpy(upgrade, m_server->m_ws_upgrade);
	strcpy(connection, m_server->m_ws_connection);
	strcpy(key, m_server->m_ws_key);

	// Assert that we have all headers that are needed. If so, go ahead and send response headers.
	if(upgrade[0] != 0 && host[0] != 0 && connection[0] != 0 && version == 13) {
        strcat_P(key, PSTR("258EAFA5-E914-47DA-95CA-C5AB0DC85B11")); // Add the omni-valid GUID
        Sha1.init();
        Sha1.print(key);
        uint8_t *hash = Sha1.result();
        base64_encode(temp, (char*)hash, 20);
		char buf[256];

		strcpy_P(buf, PSTR("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "));
		strcat(buf, temp);
		if(strcmp(m_protocol, "") != 0) {
			strcat_P( buf, PSTR("\r\nSec-WebSocket-Protocol: "));
			strcat( buf, m_protocol );
		}
		strcat_P(buf, PSTR("\r\n\r\n"));
		m_client.print( buf );
		DEBUG_WEBSOCKETS(F("*** connection %d upgraded to WEBSOCKET ***\n"), m_index);
		ret = true;
    } else {
        // Nope, failed handshake. Disconnect
		DEBUG_WEBSOCKETS(F(" failed! Upgrade: %s, Connection: %s, Host: %s, Key: %s, Version: %d, Index: %d"), upgrade, connection, host, key, version, m_index);
    }
    return ret;
}

bool WebSocket::getFrame() {
	bool ret = false;
    byte bite;

    // Get opcode
    bite = m_client.read();				// read first char: final frame bit

    frame.opcode = bite & 0x0F; 		// Opcode
    frame.isFinal = bite & 0x80; 		// Final frame?
    // Determine length (only accept <= 64 for now)
    bite = m_client.read();				// read second byte:  if masked (0x80) and data length (0x7F)
    frame.length = bite & 0x7f; 		// Length of payload
    if(frame.length > 64) {
		DEBUG_WEBSOCKETS(F("Too big frame to handle. Length: %d"), frame.length);
        m_client.write((uint8_t) 0x08);
        m_client.write((uint8_t) 0x02);
        m_client.write((uint8_t) 0x03);
        m_client.write((uint8_t) 0xf1);
        return ret;
    }
    // Client should always send mask, but check just to be sure
    frame.isMasked = bite & 0x80;
    if(frame.isMasked) {
        frame.mask[0] = m_client.read();
        frame.mask[1] = m_client.read();
        frame.mask[2] = m_client.read();
        frame.mask[3] = m_client.read();
    }

    // Clear any frame data that may have come previously
	memset(frame.data, 0, sizeof(frame.data)/sizeof(char));
    // Get message bytes and unmask them if necessary
    for(int i = 0; i < frame.length; i++) {				// 0...3
		if(frame.isMasked) {
			uint8_t c = m_client.read();
            frame.data[i] = c ^ frame.mask[i%4];		// 0, 1, 2, 3
        } else {
            frame.data[i] = m_client.read();
        }
    }
    // Frame complete!
    if(!frame.isFinal) {
        // We don't handle fragments! Close and disconnect.
		DEBUG_WEBSOCKETS(F("Non-final frame, doesn't handle that.\n"));
        m_client.write((uint8_t) 0x08);
        m_client.write((uint8_t) 0x02);
        m_client.write((uint8_t) 0x03);
        m_client.write((uint8_t) 0xf1);
        return ret;
    }
    switch(frame.opcode) {
        case 0x01: // Txt frame
            // Call the user provided function
 			DEBUG_WEBSOCKETS(F("Text frame received: '%s' len: %d\n"), frame.data, frame.length);
			if( m_server->onData ) {
				m_server->onData(*this, frame.data, frame.length);
			}
			ret = true;
            break;
            
        case 0x08:
            // Close frame. Answer with close and terminate tcp connection
            // TODO: Receive all bytes the client might send before closing? No?
			DEBUG_WEBSOCKETS(F("Close frame received. Closing in answer.\n"));
            m_client.write((uint8_t) 0x08);
            m_client.write((uint8_t) 0x00);
			break;

        case 0x09:		// PING control frame
			DEBUG_WEBSOCKETS(F("PING received by client %d, sending PONG...\n"), m_index);
            m_client.write((uint8_t) 0x8A);
            m_client.write((uint8_t) 0x00);
			ret = true;
			break;
		
        case 0x0A:		// PONG control frame
			DEBUG_WEBSOCKETS(F("PONG received by client %d @ %lu\n"), m_index, millis());
			resetPongMillis();
			ret = true;
			break;
            
        default:
            // Unexpected. Ignore. Probably should blow up entire universe here, but who cares.
			DEBUG_WEBSOCKETS(F("Unhandled frame ignored.\n"));
            break;
	}
    return ret;
}

bool WebSocket::send(char *data, byte length) {
	if( state != CONNECTED ) {
		return false;
	}
	m_client.write((uint8_t) 0x81); 		// Txt frame opcode
	m_client.write((uint8_t) length); 		// Length of data
	m_client.write( (const uint8_t *)data, length );
	return true;
}

