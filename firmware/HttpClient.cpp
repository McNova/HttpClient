#include "HttpClient.h"
#include "application.h"

//#define LOGGING
static const uint16_t TIMEOUT = 3000; // Allow maximum 3s between data packets.

/**
* Constructor.
*/
HttpClient::HttpClient(char* buffer, int buflen)
{
    this->buffer = buffer;
    this->buflen = buflen;
}

/**
* Method to send a header, should only be called from within the class.
*/
void HttpClient::sendHeader(const char* aHeaderName, const char* aHeaderValue)
{
    client.print(aHeaderName);
    client.print(": ");
    client.println(aHeaderValue);

    #ifdef LOGGING
    Serial.print(aHeaderName);
    Serial.print(": ");
    Serial.println(aHeaderValue);
    #endif
}

void HttpClient::sendHeader(const char* aHeaderName, const int aHeaderValue)
{
    client.print(aHeaderName);
    client.print(": ");
    client.println(aHeaderValue);

    #ifdef LOGGING
    Serial.print(aHeaderName);
    Serial.print(": ");
    Serial.println(aHeaderValue);
    #endif
}

void HttpClient::sendHeader(const char* aHeaderName)
{
    client.println(aHeaderName);

    #ifdef LOGGING
    Serial.println(aHeaderName);
    #endif
}

/**
* Method to send an HTTP Request. Allocate variables in your application code
* in the aResponse struct and set the headers and the options in the aRequest
* struct.
*/
void HttpClient::request(http_request_t &aRequest, http_response_t &aResponse, http_header_t headers[], const char* aHttpMethod)
{
    // If a proper response code isn't received it will be set to -1.
    aResponse.status = -1;

    // NOTE: The default port tertiary statement is unpredictable if the request structure is not initialised
    // http_request_t request = {0} or memset(&request, 0, sizeof(http_request_t)) should be used
    // to ensure all fields are zero
    if(aRequest.hostname!=NULL) {
        client.connect(aRequest.hostname.c_str(), (aRequest.port) ? aRequest.port : 80 );
    }   else {
        client.connect(aRequest.ip, aRequest.port);
    }

    #ifdef LOGGING
    if (client.connected()) {
        if(aRequest.hostname!=NULL) {
            Serial.print("HttpClient>\tConnecting to: ");
            Serial.print(aRequest.hostname);
        } else {
            Serial.print("HttpClient>\tConnecting to IP: ");
            Serial.print(aRequest.ip);
        }
        Serial.print(":");
        Serial.println(aRequest.port);
    } else {
        Serial.println("HttpClient>\tConnection failed.");
    }
    #endif

    if (!client.connected()) {
        client.stop();
        // If TCP Client can't connect to host, exit here.
        return;
    }

    //
    // Send HTTP Headers
    //

    // Send initial headers (only HTTP 1.0 is supported for now).
    client.print(aHttpMethod);
    client.print(" ");
    client.print(aRequest.path);
    client.print(" HTTP/1.0\r\n");

    #ifdef LOGGING
    Serial.println("HttpClient>\tStart of HTTP Request.");
    Serial.print(aHttpMethod);
    Serial.print(" ");
    Serial.print(aRequest.path);
    Serial.print(" HTTP/1.0\r\n");
    #endif

    // Send General and Request Headers.
    sendHeader("Connection", "close"); // Not supporting keep-alive for now.
    if(aRequest.hostname!=NULL) {
        sendHeader("HOST", aRequest.hostname.c_str());
    }

    //Send Entity Headers
    // TODO: Check the standard, currently sending Content-Length : 0 for empty
    // POST requests, and no content-length for other types.
    if (aRequest.body != NULL) {
        sendHeader("Content-Length", (aRequest.body).length());
    } else if (strcmp(aHttpMethod, HTTP_METHOD_POST) == 0) { //Check to see if its a Post method.
        sendHeader("Content-Length", 0);
    }

    if (headers != NULL)
    {
        int i = 0;
        while (headers[i].header != NULL)
        {
            if (headers[i].value != NULL) {
                sendHeader(headers[i].header, headers[i].value);
            } else {
                sendHeader(headers[i].header);
            }
            i++;
        }
    }

    // Empty line to finish headers
    client.println();
    client.flush();

    //
    // Send HTTP Request Body
    //

    if (aRequest.body != NULL) {
        client.println(aRequest.body);
        client.flush();

        #ifdef LOGGING
        Serial.println(aRequest.body);
        #endif
    }

    #ifdef LOGGING
    Serial.println("HttpClient>\tEnd of HTTP Request.");
    #endif

    // clear response buffer
    memset(buffer, 0, buflen);
    aResponse.body = buffer;
    aResponse.length = -1;

    //
    // Receive HTTP Response
    //
    // The first value of client.available() might not represent the
    // whole response, so after the first chunk of data is received instead
    // of terminating the connection there is a delay and another attempt
    // to read data.
    // The loop exits when the connection is closed, or if there is a
    // timeout or an error.

    unsigned int bufferPosition = 0;
    unsigned long lastRead = millis();
    unsigned long firstRead = millis();
    bool error = false;
    bool timeout = false;
    char* header;

    do {
        #ifdef LOGGING
        int bytes = client.available();
        if(bytes) {
            Serial.print("\r\nHttpClient>\tReceiving TCP transaction of ");
            Serial.print(bytes);
            Serial.println(" bytes.");
        }
        #endif

        while (client.available()) {
            char c = client.read();
            #ifdef LOGGING
            Serial.print(c);
            #endif
            lastRead = millis();
            Spark.process();

            if (c == -1) {
                error = true;

                #ifdef LOGGING
                Serial.println("HttpClient>\tError: No data available.");
                #endif

                break;
            }

            // Check that received character fits in buffer before storing.
            if (bufferPosition < buflen-1) {
                buffer[bufferPosition++] = c;
            } else if ((bufferPosition == buflen-1)) {
                buffer[bufferPosition] = '\0'; // Null-terminate buffer
                client.stop();
                error = true;

                #ifdef LOGGING
                Serial.println("\r\nHttpClient>\tError: Response body larger than buffer.");
                #endif
                
                break;
            }

            buffer[bufferPosition] = '\0';

            if (c == 10) {
                if (strncmp(header, "Content-Length: ", 16) == 0) {
                    aResponse.length = atoi(&header[16]);
                }
                header = &buffer[bufferPosition];
            }
            
            aResponse.body = strstr(buffer, "\r\n\r\n");
            if (aResponse.body != NULL)
            {
                aResponse.body += 4;
                if (strlen(aResponse.body) == aResponse.length)
                {
                    client.stop();
                }
            }
        }

        #ifdef LOGGING
        if (bytes) {
            Serial.println("\r\nHttpClient>\tEnd of TCP transaction.");
        }
        #endif

        // Check that there hasn't been more than 3s since last read.
        timeout = millis() - lastRead > TIMEOUT;

        // Unless there has been an error or timeout wait 200ms to allow server
        // to respond or close connection.
        if (!error && !timeout) {
            //delay(200);
            Spark.process();
        }
    } while (client.connected() && !timeout && !error);
    
    #ifdef LOGGING
    if (timeout) {
        Serial.println("\r\nHttpClient>\tError: Timeout while reading response.");
    }
    Serial.print("\r\nHttpClient>\tEnd of HTTP Response (");
    Serial.print(millis() - firstRead);
    Serial.println("ms).");
    #endif
    client.stop();
    
    // Not super elegant way of finding the status code, but it works.
    buffer[12] = 0;
    aResponse.status = atoi(&buffer[9]);
    #ifdef LOGGING
    Serial.println(aResponse.status);
    Serial.println(aResponse.body);
    #endif
    if (aResponse.body == NULL) {
        #ifdef LOGGING
        Serial.println("HttpClient>\tError: Can't find HTTP response body.");
        #endif
        return;
    }
    aResponse.length = strlen(aResponse.body);
}
