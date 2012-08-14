/*
 Meanwhile - Unofficial Lotus Sametime Community Client Library
 Copyright (C) 2004  Christopher (siege) O'Brien
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Library General Public
 License as published by the Free Software Foundation; either
 version 2 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Library General Public License for more details.
 
 You should have received a copy of the GNU Library General Public
 License along with this library; if not, write to the Free
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 @file socket.c
 
 This file is a simple demonstration of using unix socket code to
 connect a mwSession to a sametime server and get it fully logged
 in. It doesn't do anything after logging in.
 
 Here you'll find examples of:
 - opening a socket to the host
 - using the socket to feed data to the session
 - using a session handler to allow the session to write data to the
 socket
 - using a session handler to allow the session to close the socket
 - watching for error conditions on read/write
 */

/*
 * NOTE: This is a port of the Meanwhile socket.c code to use the 
 * CFSocket library.
 *
 * Send any bugs to Chris Parker <mrcsparker@gmail.com>
 */

#import <CoreFoundation/CFSocket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>

#include <mw_common.h>
#include <mw_session.h>



/** help text if you don't give the right number of arguments */
#define HELP \
"Meanwhile sample socket client\n" \
"Usage: %s server userid password\n" \
"\n" \
"Connects to a sametime server and logs in with the supplied user ID\n" \
"and password. Doesn't actually do anything useful after that.\n\n"



/** how much to read from the socket in a single call */
#define BUF_LEN 2048



/* client data should be put into a structure and associated with the
 session. Then it will be available from the many call-backs
 handling events from the session */
struct sample_client {
    struct mwSession *session;  /* the actual meanwhile session */
    CFSocketRef sock;                   /* the socket connecting to the server */
};



/* the io_close function from the session handler */
static void mw_session_io_close(struct mwSession *session) {
    struct sample_client *client;
    
    /* get the client data from the session */
    client = mwSession_getClientData(session);
    
    /* safety check */
    g_return_if_fail(client != NULL);
}



/* the io_write function from the session handler */
static int mw_session_io_write(struct mwSession *session,
                               const guchar *buf, gsize len) {
    
    fprintf(stderr, "mw_session_io_write: %d\n", len);
    
    CFSocketError socketErrors;
    struct sample_client *client;
    
    /* get the client data from the session */
    client = mwSession_getClientData(session);
    
    /* safety check */
    g_return_val_if_fail(client != NULL, -1);
    
    /* socket was already closed, so we can't possibly write to it */
    if(client->sock == NULL) return -1;
 
    if (len > 0) {
        
        fprintf(stderr, "Writing data\n");
        
        CFDataRef data = CFDataCreate(NULL, buf, len);
    
        socketErrors = CFSocketSendData(client->sock, NULL, data, 0);
        if (socketErrors < 0) {
            fprintf(stderr, "Error sending data\n");
        }
        
        CFRelease(data);
    }
 
    /* return success code */
    return 0;
}



/* the on_stateChange function from the session handler */
static void mw_session_stateChange(struct mwSession *session,
                                   enum mwSessionState state,
                                   gpointer info) {
    
    fprintf(stderr, "mw_session_stateChange\n");
    
    
    switch (state) {
        case mwSession_STARTING:
            fprintf(stderr, "[2] Sending Handshake");
            break;
        case mwSession_HANDSHAKE:
            fprintf(stderr, "[3] Waiting for Handshake Acknowledgement");
            break;
        case mwSession_HANDSHAKE_ACK:
            fprintf(stderr, "[4] Handshake Acknowledged, Sending Login");
            break;
        case mwSession_LOGIN:
            fprintf(stderr, "[5] Waiting for Login Acknowledgement");
            break;
        case mwSession_LOGIN_REDIR:
            fprintf(stderr, "[6] Login redirected");
            break;
        case mwSession_LOGIN_CONT:
            fprintf(stderr, "[7] Forcing login");
            break;
        case mwSession_LOGIN_ACK:
            fprintf(stderr, "[8] Login Acknowledged");
            break;
        case mwSession_STARTED:
            fprintf(stderr, "[9] Starting services");
            break;
        case mwSession_STOPPING:
            fprintf(stderr, "Stopping session");
            break;
        case mwSession_STOPPED:
            fprintf(stderr, "Session stopped");
            break;
        case mwSession_UNKNOWN:
            fprintf(stderr, "Session unknown.  Your guess is as good as mine!");
            break;
        default:
            fprintf(stderr, "Session in unknown state.  You are in uncharted territory");
            break;
    }
}



/* the session handler structure is where you should indicate what
 functions will perform many of the functions necessary for the
 session to operate. Among these, only io_write and io_close are
 absolutely required. */
static struct mwSessionHandler mw_session_handler = {
    .io_write = mw_session_io_write,  /**< handle session to socket */
    .io_close = mw_session_io_close,  /**< handle session closing socket */
    .clear = NULL,                    /**< cleanup function */
    .on_stateChange = mw_session_stateChange,  /**< session status changed */
    .on_setPrivacyInfo = NULL,        /**< received privacy information */
    .on_setUserStatus = NULL,         /**< received status information */
    .on_admin = NULL,                 /**< received an admin message */
};



/** called from read_cb, attempts to read available data from sock and
 pass it to the session. Returns zero when the socket has been
 closed, less-than zero in the event of an error, and greater than
 zero for success */
static void read_cb(CFSocketRef s,
                    CFSocketCallBackType type,
                    CFDataRef address,
                    const void *data,
                    void *info)
{
    fprintf(stderr, "read_cb\n");
    fprintf(stderr, "Callback type %ld\n", type);
    
    CFDataRef df = (CFDataRef) data;
    long len = CFDataGetLength(df);
    
    struct mwSession *session = info;
    
    if (len <= 0) {
        fprintf(stderr, "No data\n");
        return;
    }
    
    CFRange range = CFRangeMake(0, len);
    UInt8 buffer[len];
    
    fprintf(stderr, "Received %ld bytes from socket %d\n", len, CFSocketGetNative(s));
    
    CFDataGetBytes(df, range, buffer);
    
    mwSession_recv(session, buffer, len);
    
    fprintf(stderr, "Client received: %s\n", buffer);
}



/* open and return a network socket fd connected to host:port */
static CFSocketRef init_sock(const char *host, int port, struct mwSession *session) {
    
    fprintf(stderr, "init_sock\n");
    
    CFSocketRef s;
    struct sockaddr_in addr;
    CFSocketContext cxt = { 0, session, NULL, NULL, NULL };
    struct hostent *hostinfo;
    
    s = CFSocketCreate(NULL,
                       PF_INET,
                       SOCK_STREAM,
                       IPPROTO_TCP,
                       kCFSocketDataCallBack,
                       read_cb,
                       &cxt);
    
    if (s == NULL) {
        fprintf(stderr, "Unable to connect\n");
    }
    
    fprintf(stderr, "Created socket\n");
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons (port);
    hostinfo = gethostbyname(host);
    if(hostinfo == NULL) {
        fprintf(stderr, "Unknown host %s.\n", host);
    }
    addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;
    
    CFDataRef address = CFDataCreate(NULL, (UInt8 *)&addr, sizeof(addr));
    CFSocketError error = CFSocketConnectToAddress(s, address, 0);
    if (error < 0) {
        fprintf(stderr, "Error connecting to address\n");
    }
    CFRelease(address);
    
    fprintf(stderr, "init_sock\n");
    
    return s;
}



int main(int argc, char *argv[]) {
    
    fprintf(stderr, "Starting app\n");
    
    /* the meanwhile session itself */
    struct mwSession *session;
    
    /* client program data */
    struct sample_client *client;
    
    /* specify host, user, pass on the command line */
    if(argc != 4) {
        fprintf(stderr, HELP, *argv);
        return 1;
    }
    
    /* create the session and set the user and password */
    session = mwSession_new(&mw_session_handler);
    mwSession_setProperty(session, mwSession_AUTH_USER_ID, argv[2], NULL);
    mwSession_setProperty(session, mwSession_AUTH_PASSWORD, argv[3], NULL);
    
    /* create the client data. This is arbitrary data that a client will
     want to store along with the session for its own use */
    client = g_new0(struct sample_client, 1);
    client->session = session;
    
    /* associate the client data with the session, specifying an
     optional cleanup function which will be called upon the data when
     the session is free'd via mwSession_free */
    mwSession_setClientData(session, client, g_free);
    
    
    /* set up a connection to the host */
    client->sock = init_sock(argv[1], 1533, session);
    
    /* start the session. This will cause the session to send the
     handshake message (using the io_write function specified in the
     session handler). */
    mwSession_start(session);
    
    CFRunLoopSourceRef source = CFSocketCreateRunLoopSource(NULL, client->sock, 0);
    if (source == NULL) {
        fprintf(stderr, "DCSocket::Failed to create runloop source" );
    }
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);

    fprintf(stderr, "\nCreated run loop\n");
    
    CFRelease(source);
    CFRelease(client->sock);
    
    CFRunLoopRun();
    
    /* this won't happen until after the main loop finally terminates */
    return 0;
}


