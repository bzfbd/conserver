/*
 *  $Id: console.c,v 5.137 2003-09-22 08:23:57-07 bryan Exp $
 *
 *  Copyright conserver.com, 2000
 *
 *  Maintainer/Enhancer: Bryan Stansell (bryan@conserver.com)
 *
 *  Copyright GNAC, Inc., 1998
 */

/*
 * Copyright (c) 1990 The Ohio State University.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment, and (2) distributions including binaries display
 * the following acknowledgement:  ``This product includes software
 * developed by The Ohio State University and its contributors''
 * in the documentation or other materials provided with the distribution
 * and in all advertising materials mentioning features or use of this
 * software. Neither the name of the University nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <compat.h>

#include <pwd.h>

#include <getpassword.h>
#include <util.h>
#include <version.h>
#if HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>
#endif


int fReplay = 0, fVersion = 0, fStrip = 0;
#if HAVE_OPENSSL
int fReqEncryption = 1;
char *pcCredFile = (char *)0;
#endif
int chAttn = -1, chEsc = -1;
char *pcInMaster =		/* which machine is current */
    MASTERHOST;
char *pcPort = DEFPORT;
unsigned short bindPort;
CONSFILE *cfstdout;
char *pcUser = (char *)0;
int disconnectCount = 0;

static char acMesg[8192];	/* the buffer for startup negotiation   */

#if HAVE_OPENSSL
SSL_CTX *ctx = (SSL_CTX *)0;

void
#if PROTOTYPES
SetupSSL(void)
#else
SetupSSL()
#endif
{
    if (ctx == (SSL_CTX *)0) {
	SSL_load_error_strings();
	if (!SSL_library_init()) {
	    Error("SSL library initialization failed");
	    exit(EX_UNAVAILABLE);
	}
	if ((ctx = SSL_CTX_new(SSLv23_method())) == (SSL_CTX *)0) {
	    Error("Creating SSL context failed");
	    exit(EX_UNAVAILABLE);
	}
	if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
	    Error("Could not load SSL default CA file and/or directory");
	    exit(EX_UNAVAILABLE);
	}
	if (pcCredFile != (char *)0) {
	    if (SSL_CTX_use_certificate_chain_file(ctx, pcCredFile) != 1) {
		Error("Could not load SSL certificate from '%s'",
		      pcCredFile);
		exit(EX_UNAVAILABLE);
	    }
	    if (SSL_CTX_use_PrivateKey_file
		(ctx, pcCredFile, SSL_FILETYPE_PEM) != 1) {
		Error("Could not SSL private key from '%s'", pcCredFile);
		exit(EX_UNAVAILABLE);
	    }
	}
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, SSLVerifyCallback);
	SSL_CTX_set_options(ctx,
			    SSL_OP_ALL | SSL_OP_NO_SSLv2 |
			    SSL_OP_SINGLE_DH_USE);
	SSL_CTX_set_mode(ctx,
			 SSL_MODE_ENABLE_PARTIAL_WRITE |
			 SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER |
			 SSL_MODE_AUTO_RETRY);
	if (SSL_CTX_set_cipher_list(ctx, "ALL:!LOW:!EXP:!MD5:@STRENGTH") !=
	    1) {
	    Error("Setting SSL cipher list failed");
	    exit(EX_UNAVAILABLE);
	}
    }
}

void
#if PROTOTYPES
AttemptSSL(CONSFILE *pcf)
#else
AttemptSSL(pcf)
    CONSFILE *pcf;
#endif
{
    SSL *ssl;

    if (ctx == (SSL_CTX *)0) {
	Error("WTF?  The SSL context disappeared?!?!?");
	exit(EX_UNAVAILABLE);
    }
    if (!(ssl = SSL_new(ctx))) {
	Error("Couldn't create new SSL context");
	exit(EX_UNAVAILABLE);
    }
    FileSetSSL(pcf, ssl);
    SSL_set_fd(ssl, FileFDNum(pcf));
    CONDDEBUG((1, "About to SSL_connect() on fd %d", FileFDNum(pcf)));
    if (SSL_connect(ssl) <= 0) {
	Error("SSL negotiation failed");
	ERR_print_errors_fp(stderr);
	exit(EX_UNAVAILABLE);
    }
    FileSetType(pcf, SSLSocket);
    CONDDEBUG((1, "SSL Connection: %s :: %s", SSL_get_cipher_version(ssl),
	       SSL_get_cipher_name(ssl)));
}
#endif

void
#if PROTOTYPES
DestroyDataStructures(void)
#else
DestroyDataStructures()
#endif
{
}

/* output a control (or plain) character as a UNIX user would expect it	(ksb)
 */
static void
#if PROTOTYPES
PutCtlc(int c, FILE *fp)
#else
PutCtlc(c, fp)
    int c;
    FILE *fp;
#endif
{
    if (0 != (0200 & c)) {
	putc('M', fp);
	putc('-', fp);
	c &= ~0200;
    }
    if (isprint(c)) {
	putc(c, fp);
	return;
    }
    putc('^', fp);
    if (c == 0177) {
	putc('?', fp);
	return;
    }
    putc(c + 0100, fp);
}

/* output a long message to the user
 */
static void
#if PROTOTYPES
Usage(int wantfull)
#else
Usage(wantfull)
    int wantfull;
#endif
{
    static char *full[] = {
	"7       strip the high bit of all console data",
	"a(A)    attach politely (and replay last 20 lines)",
	"b(B)    send broadcast message to all users (on master)",
#if HAVE_OPENSSL
	"c cred  load an SSL certificate and key from the PEM encoded file",
#else
	"c cred  ignored - encryption not compiled into code",
#endif
	"d       disconnect [user][@console]",
	"D       enable debug output, sent to stderr",
	"e esc   set the initial escape characters",
#if HAVE_OPENSSL
	"E       don't require encrypted connections",
#else
	"E       ignored - encryption not compiled into code",
#endif
	"f(F)    force read/write connection (and replay)",
	"i(I)    display information in machine-parseable form (on master)",
	"h       output this message",
	"l user  use username instead of current username",
	"M mach  master server to poll first",
	"p port  port to connect to",
	"P       display pids of daemon(s)",
	"q(Q)    send a quit command to the (master) server",
	"r(R)    display (master) daemon version (think 'r'emote version)",
	"s(S)    spy on a console (and replay)",
	"t       send a text message to [user][@console]",
	"u       show users on the various consoles",
	"v       be more verbose",
	"V       show version information",
	"w(W)    show who is on which console (on master)",
	"x       examine ports and baud rates",
	(char *)0
    };

    fprintf(stderr,
	    "usage: %s [-aAEfFsS] [-7Dv] [-c cred] [-M mach] [-p port] [-e esc] [-l username] console\n",
	    progname);
    fprintf(stderr,
	    "usage: %s [-hiIPrRuVwWx] [-7Dv] [-M mach] [-p port] [-d [user][@console]] [-[bB] message] [-t [user][@console] message]\n",
	    progname);
    fprintf(stderr, "usage: %s [-qQ] [-7Dv] [-M mach] [-p port]\n",
	    progname);

    if (wantfull) {
	int i;
	for (i = 0; full[i] != (char *)0; i++)
	    fprintf(stderr, "\t%s\n", full[i]);
    }
}

/* expain who we are and which revision we are				(ksb)
 */
static void
#if PROTOTYPES
Version()
#else
Version()
#endif
{
    int i;
    static STRING *acA1 = (STRING *)0;
    static STRING *acA2 = (STRING *)0;
    char *optionlist[] = {
#if HAVE_DMALLOC
	"dmalloc",
#endif
#if USE_LIBWRAP
	"libwrap",
#endif
#if HAVE_OPENSSL
	"openssl",
#endif
#if HAVE_PAM
	"pam",
#endif
	(char *)0
    };

    if (acA1 == (STRING *)0)
	acA1 = AllocString();
    if (acA2 == (STRING *)0)
	acA2 = AllocString();

    Msg("%s", THIS_VERSION);
    Msg("default initial master server `%s\'", MASTERHOST);
    Msg("default escape sequence `%s%s\'", FmtCtl(DEFATTN, acA1),
	FmtCtl(DEFESC, acA2));
    Msg("default port referenced as `%s'", DEFPORT);

    BuildString((char *)0, acA1);
    if (optionlist[0] == (char *)0)
	BuildString("none", acA1);
    for (i = 0; optionlist[i] != (char *)0; i++) {
	if (i == 0)
	    BuildString(optionlist[i], acA1);
	else {
	    BuildString(", ", acA1);
	    BuildString(optionlist[i], acA1);
	}
    }
    Msg("options: %s", acA1->string);
#if HAVE_DMALLOC
    BuildString((char *)0, acA1);
    BuildStringChar('0' + DMALLOC_VERSION_MAJOR, acA1);
    BuildStringChar('.', acA1);
    BuildStringChar('0' + DMALLOC_VERSION_MINOR, acA1);
    BuildStringChar('.', acA1);
    BuildStringChar('0' + DMALLOC_VERSION_PATCH, acA1);
    if (DMALLOC_VERSION_BETA != 0) {
	BuildString("-b", acA1);
	BuildStringChar('0' + DMALLOC_VERSION_BETA, acA1);
    }
    Msg("dmalloc version: %s", acA1->string);
#endif
#if HAVE_OPENSSL
    Msg("openssl version: %s", OPENSSL_VERSION_TEXT);
#endif
    Msg("built with `%s'", CONFIGINVOCATION);
    if (fVerbose)
	printf(COPYRIGHT);
}


/* convert text to control chars, we take `cat -v' style		(ksb)
 *	^X (or ^x)		contro-x
 *	M-x			x plus 8th bit
 *	c			a plain character
 */
static int
#if PROTOTYPES
ParseChar(char **ppcSrc, char *pcOut)
#else
ParseChar(ppcSrc, pcOut)
    char **ppcSrc, *pcOut;
#endif
{
    int cvt, n;
    char *pcScan = *ppcSrc;

    if ('M' == pcScan[0] && '-' == pcScan[1] && '\000' != pcScan[2]) {
	cvt = 0x80;
	pcScan += 2;
    } else {
	cvt = 0;
    }

    if ('\000' == *pcScan) {
	return 1;
    }

    if ('^' == (n = *pcScan++)) {
	if ('\000' == (n = *pcScan++)) {
	    return 1;
	}
	if (islower(n)) {
	    n = toupper(n);
	}
	if ('@' <= n && n <= '_') {
	    cvt |= n - '@';
	} else if ('?' == *pcScan) {
	    cvt |= '\177';
	} else {
	    return 1;
	}
    } else {
	cvt |= n;
    }

    if ((char *)0 != pcOut) {
	*pcOut = cvt;
    }
    *ppcSrc = pcScan;
    return 0;
}

/*
 */
static void
#if PROTOTYPES
ValidateEsc()
#else
ValidateEsc()
#endif
{
    unsigned char c1, c2;

    if (!fStrip)
	return;

    if (chAttn == -1 || chEsc == -1) {
	c1 = DEFATTN;
	c2 = DEFESC;
    } else {
	c1 = chAttn;
	c2 = chEsc;
    }
    if (c1 > 127 || c2 > 127) {
	Error("High-bit set in escape sequence: not allowed with -7");
	exit(EX_UNAVAILABLE);
    }
}

/* find the two characters that makeup the users escape sequence	(ksb)
 */
static void
#if PROTOTYPES
ParseEsc(char *pcText)
#else
ParseEsc(pcText)
    char *pcText;
#endif
{
    char *pcTemp;
    char c1, c2;

    pcTemp = pcText;
    if (ParseChar(&pcTemp, &c1) || ParseChar(&pcTemp, &c2)) {
	Error("poorly formed escape sequence `%s\'", pcText);
	exit(EX_UNAVAILABLE);
    }
    if ('\000' != *pcTemp) {
	Error("too many characters in new escape sequence at ...`%s\'",
	      pcTemp);
	exit(EX_UNAVAILABLE);
    }
    chAttn = c1;
    chEsc = c2;
}


/* set the port for socket connection					(ksb)
 * return the fd for the new connection; if we can use the loopback, do
 * as a side effect we set ThisHost to a short name for this host
 */
CONSFILE *
#if PROTOTYPES
GetPort(char *pcToHost, unsigned short sPort)
#else
GetPort(pcToHost, sPort)
    char *pcToHost;
    unsigned short sPort;
#endif
{
    int s;
    struct hostent *hp = (struct hostent *)0;
    struct sockaddr_in port;

#if HAVE_MEMSET
    memset((void *)(&port), '\000', sizeof(port));
#else
    bzero((char *)(&port), sizeof(port));
#endif

#if HAVE_INET_ATON
    if (inet_aton(pcToHost, &(port.sin_addr)) == 0)
#else
    port.sin_addr.s_addr = inet_addr(pcToHost);
    if ((in_addr_t) (-1) == port.sin_addr.s_addr)
#endif
    {
	if ((struct hostent *)0 != (hp = gethostbyname(pcToHost))) {
#if HAVE_MEMCPY
	    memcpy((char *)&port.sin_addr.s_addr, (char *)hp->h_addr,
		   hp->h_length);
#else
	    bcopy((char *)hp->h_addr, (char *)&port.sin_addr.s_addr,
		  hp->h_length);
#endif
	} else {
	    Error("gethostbyname(%s): %s", pcToHost, hstrerror(h_errno));
	    return (CONSFILE *)0;
	}
    }
    port.sin_port = sPort;
    port.sin_family = AF_INET;

    if (fDebug) {
	if ((struct hostent *)0 != hp && (char *)0 != hp->h_name) {
	    CONDDEBUG((1, "GetPort: hostname=%s (%s), ip=%s, port=%hu",
		       hp->h_name, pcToHost, inet_ntoa(port.sin_addr),
		       ntohs(sPort)));
	} else {
	    CONDDEBUG((1,
		       "GetPort: hostname=<unresolved> (%s), ip=%s, port=%hu",
		       pcToHost, inet_ntoa(port.sin_addr), ntohs(sPort)));
	}
    }

    /* set up the socket to talk to the server for all consoles
     * (it will tell us who to talk to to get a real connection)
     */
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	Error("socket(AF_INET,SOCK_STREAM): %s", strerror(errno));
	return (CONSFILE *)0;
    }
    if (connect(s, (struct sockaddr *)(&port), sizeof(port)) < 0) {
	Error("connect(): %hu@%s: %s", ntohs(port.sin_port), pcToHost,
	      strerror(errno));
	return (CONSFILE *)0;
    }

    return FileOpenFD(s, simpleSocket);
}


/* the next two routines assure that the users tty is in the
 * correct mode for us to do our thing
 */
static int screwy = 0;
static struct termios o_tios;


/*
 * show characters that are already tty processed,
 * and read characters before cononical processing
 * we really use cbreak at PUCC because we need even parity...
 */
static void
#if PROTOTYPES
C2Raw()
#else
C2Raw()
#endif
{
    struct termios n_tios;

    if (!isatty(0) || 0 != screwy)
	return;

    if (0 != tcgetattr(0, &o_tios)) {
	Error("tcgetattr(0): %s", strerror(errno));
	exit(EX_UNAVAILABLE);
    }
    n_tios = o_tios;
    n_tios.c_iflag &= ~(INLCR | IGNCR | ICRNL | IUCLC | IXON);
    n_tios.c_oflag &= ~OPOST;
    n_tios.c_lflag &= ~(ICANON | ISIG | ECHO | IEXTEN);
    n_tios.c_cc[VMIN] = 1;
    n_tios.c_cc[VTIME] = 0;
    if (0 != tcsetattr(0, TCSANOW, &n_tios)) {
	Error("tcsetattr(0, TCSANOW): %s", strerror(errno));
	exit(EX_UNAVAILABLE);
    }
    screwy = 1;
}

/*
 * put the tty back as it was, however that was
 */
static void
#if PROTOTYPES
C2Cooked()
#else
C2Cooked()
#endif
{
    if (!screwy)
	return;
    tcsetattr(0, TCSANOW, &o_tios);
    screwy = 0;
}

char *
#if PROTOTYPES
ReadReply(CONSFILE *fd, int toEOF)
#else
ReadReply(fd)
    CONSFILE *fd;
    int toEOF;
#endif
{
    int nr;
    static char buf[1024];
    static STRING *result = (STRING *)0;

    if (result == (STRING *)0)
	result = AllocString();
    else
	BuildString((char *)0, result);

    while (1) {
	switch (nr = FileRead(fd, buf, sizeof(buf))) {
	    case 0:
		/* fall through */
	    case -1:
		if (result->used > 1 || toEOF)
		    break;
		C2Cooked();
		Error("lost connection");
		exit(EX_UNAVAILABLE);
	    default:
		BuildStringN(buf, nr, result);
		if (toEOF)	/* if toEOF, read until EOF */
		    continue;
		if ((result->used > 1) &&
		    (result->string[result->used - 2] == '\n'))
		    break;
		continue;
	}
	break;
    }
    if (fDebug) {
	static STRING *tmpString = (STRING *)0;
	if (tmpString == (STRING *)0)
	    tmpString = AllocString();
	BuildString((char *)0, tmpString);
	FmtCtlStr(result->string, result->used - 1, tmpString);
	CONDDEBUG((1, "ReadReply: `%s'", tmpString->string));
    }
    return result->string;
}

static int SawUrg = 0;

/* when the conserver program gets the suspend sequence it will send us
 * an out of band command to suspend ourself.  We just tell the reader
 * routine we saw one
 */
RETSIGTYPE
#if PROTOTYPES
OOB(int sig)
#else
OOB(sig)
    int sig;
#endif
{
    ++SawUrg;
#if !HAVE_SIGACTION
#if defined(SIGURG)
    SimpleSignal(SIGURG, OOB);
#endif
#endif
}

void
#if PROTOTYPES
ProcessUrgentData(int s)
#else
ProcessUrgentData(s)
    int s;
#endif
{
    static char acCmd;

    SawUrg = 0;

    /* get the pending urgent message
     */
    while (recv(s, &acCmd, 1, MSG_OOB) < 0) {
	switch (errno) {
	    case EWOULDBLOCK:
		/* clear any pending input to make room */
		read(s, &acCmd, 1);
		write(1, ".", 1);
		continue;
	    case EINVAL:
	    default:
		Error("recv(%d): %s\r", s, strerror(errno));
		sleep(1);
		continue;
	}
    }
    switch (acCmd) {
	case OB_SUSP:
#if defined(SIGSTOP)
	    write(1, "stop]", 5);
	    C2Cooked();
	    kill(getpid(), SIGSTOP);
	    C2Raw();
	    write(1, "[press any character to continue", 32);
#else
	    write(1,
		  "stop not supported -- press any character to continue",
		  53);
#endif
	    break;
	case OB_DROP:
	    write(1, "dropped by server]\r\n", 20);
	    C2Cooked();
	    exit(EX_UNAVAILABLE);
	 /*NOTREACHED*/ default:
	    Error("unknown out of band command `%c\'\r", acCmd);
	    fflush(stderr);
	    break;
    }
}

/* interact with a group server					(ksb)
 */
static int
#if PROTOTYPES
CallUp(CONSFILE *pcf, char *pcMaster, char *pcMach, char *pcHow,
       char *result)
#else
CallUp(pcf, pcMaster, pcMach, pcHow, result)
    CONSFILE *pcf;
    char *pcMaster, *pcMach, *pcHow, *result;
#endif
{
    int nc;
    int fIn = '-';
    fd_set rmask, rinit;
    int i;
    int justProcessedUrg = 0;

    if (fVerbose) {
	Msg("%s to %s (on %s)", pcHow, pcMach, pcMaster);
    }
#if !defined(__CYGWIN__)
# if defined(F_SETOWN)
    if (fcntl(FileFDNum(pcf), F_SETOWN, getpid()) == -1) {
	Error("fcntl(F_SETOWN,%d): %d: %s", getpid(), FileFDNum(pcf),
	      strerror(errno));
    }
# else
#  if defined(SIOCSPGRP)
    {
	int iTemp;
	/* on the HP-UX systems if different
	 */
	iTemp = -getpid();
	if (ioctl(FileFDNum(pcf), SIOCSPGRP, &iTemp) == -1) {
	    Error("ioctl(%d,SIOCSPGRP): %s", FileFDNum(pcf),
		  strerror(errno));
	}
    }
#  endif
# endif
#endif
#if defined(SIGURG)
    SimpleSignal(SIGURG, OOB);
#endif

    /* if we are going for a particular console
     * send sign-on stuff, then wait for some indication of what mode
     * we got from the server (if we are the only people on we get write
     * access by default, which is fine for most people).
     */

    /* how did we do, did we get a read-only or read-write?
     */
    if (0 == strcmp(result, "[attached]\r\n")) {
	/* OK -- we are good as gold */
	fIn = 'a';
    } else if (0 == strcmp(result, "[spy]\r\n") ||
	       0 == strcmp(result, "[ok]\r\n")) {
	/* Humph, someone else is on
	 * or we have an old version of the server (4.X)
	 */
	fIn = 's';
    } else if (0 == strcmp(result, "[host is read-only]\r\n")) {
	fIn = 'r';
    } else if (0 == strcmp(result, "[line to host is down]\r\n")) {
	/* ouch, the machine is down on the server */
	fIn = '-';
	Error("%s is down", pcMach);
	if (fVerbose) {
	    printf("[use `");
	    PutCtlc(chAttn, stdout);
	    PutCtlc(chEsc, stdout);
	    printf("o\' to open console line]\n");
	}
    } else if (0 == strcmp(result, "[no -- on ctl]\r\n")) {
	fIn = '-';
	Error("%s is a control port", pcMach);
	if (fVerbose) {
	    printf("[use `");
	    PutCtlc(chAttn, stdout);
	    PutCtlc(chEsc, stdout);
	    printf(";\' to open a console line]\n");
	}
    } else {
	FilePrint(cfstdout, "%s: %s", pcMach, result);
	exit(EX_UNAVAILABLE);
    }

    /* change escape sequence (if set on the command line)
     * and replay the log for the user, if asked
     */
    if (chAttn == -1 || chEsc == -1) {
	chAttn = DEFATTN;
	chEsc = DEFESC;
    } else {
	char *r;
	/* tell the conserver to change escape sequences, assume OK
	 * (we'll find out soon enough)
	 */
	sprintf(acMesg, "%c%ce%c%c", DEFATTN, DEFESC, chAttn, chEsc);
	FileWrite(pcf, acMesg, 5);
	/* -bryan */
	r = ReadReply(pcf, 0);
	if (strncmp(r, "[redef:", 7) != 0) {
	    Error("protocol botch on redef of escape sequence");
	    exit(EX_UNAVAILABLE);
	}
    }

    printf("[Enter `");
    PutCtlc(chAttn, stdout);
    PutCtlc(chEsc, stdout);
    printf("?\' for help]\n");

    /* if the host is not down, finish the connection, and force
     * the correct attachment for the user
     */
    if (fIn != '-') {
	if (fIn == 'r') {
	    if (*pcHow != 's') {
		Error("%s is read-only", pcMach);
	    }
	} else if (fIn != (*pcHow == 'f' ? 'a' : *pcHow)) {
	    sprintf(acMesg, "%c%c%c", chAttn, chEsc, *pcHow);
	    FileWrite(pcf, acMesg, 3);
	}
	if (fReplay) {
	    sprintf(acMesg, "%c%cr", chAttn, chEsc);
	    FileWrite(pcf, acMesg, 3);
	} else if (fVerbose) {
	    sprintf(acMesg, "%c%c\022", chAttn, chEsc);
	    FileWrite(pcf, acMesg, 3);
	}
    }
    fflush(stdout);
    fflush(stderr);

    C2Raw();

    /* read from stdin and the socket (non-blocking!).
     * rmask indicates which descriptors to read from,
     * the others are not used, nor is the result from
     * select, read, or write.
     */
    FD_ZERO(&rinit);
    FD_SET(FileFDNum(pcf), &rinit);
    FD_SET(0, &rinit);
    if (maxfd < FileFDNum(pcf) + 1)
	maxfd = FileFDNum(pcf) + 1;
    for (;;) {
	justProcessedUrg = 0;
	if (SawUrg) {
	    ProcessUrgentData(FileFDNum(pcf));
	    justProcessedUrg = 1;
	}
	/* reset read mask and select on it
	 */
	rmask = rinit;
	if (-1 ==
	    select(maxfd, &rmask, (fd_set *)0, (fd_set *)0,
		   (struct timeval *)0)) {
	    if (errno != EINTR) {
		Error("Master(): select(): %s", strerror(errno));
		break;
	    }
	    continue;
	}

	/* anything from socket? */
	if (FD_ISSET(FileFDNum(pcf), &rmask)) {
	    if ((nc = FileRead(pcf, acMesg, sizeof(acMesg))) < 0) {
		/* if we got an error/eof after returning from suspend */
		if (justProcessedUrg) {
		    fprintf(stderr, "\n");
		    Error("lost connection");
		}
		break;
	    }
	    if (fStrip) {
		for (i = 0; i < nc; ++i)
		    acMesg[i] &= 127;
	    }
	    FileWrite(cfstdout, acMesg, nc);
	}

	/* anything from stdin? */
	if (FD_ISSET(0, &rmask)) {
	    if ((nc = read(0, acMesg, sizeof(acMesg))) == 0) {
		if (screwy)
		    break;
		else {
		    FD_SET(0, &rinit);
		    continue;
		}
	    }
	    if (fStrip) {
		for (i = 0; i < nc; ++i)
		    acMesg[i] &= 127;
	    }
	    FileWrite(pcf, acMesg, nc);
	}
    }
    C2Cooked();
    if (fVerbose)
	printf("Console %s closed.\n", pcMach);
    return 0;
}

/* shouldn't need more than 3 levels of commands (but alloc 4 just 'cause)
 * worst case so far: master, groups, broadcast
 *   (cmdarg == broadcast msg)
 */
char *cmds[4] = { (char *)0, (char *)0, (char *)0, (char *)0 };
char *cmdarg = (char *)0;

/* call a machine master for group master ports and machine master ports
 * take a list like "1782@localhost:@mentor.cc.purdue.edu:@pop.stat.purdue.edu"
 * and send the given command to the group leader at 1782
 * and ask the machine master at mentor for more group leaders
 * and ask the machine master at pop.stat for more group leaders
 */
int
#if PROTOTYPES
DoCmds(char *master, char *ports, int cmdi)
#else
DoCmds(master, ports, cmdi)
    char *master;
    char *ports;
    int cmdi;
#endif
{
    CONSFILE *pcf;
    char *t;
    char *next;
    char *server;
    unsigned short port;
    char *result = (char *)0;
    int len;

    len = strlen(ports);
    while (len > 0 && (ports[len - 1] == '\r' || ports[len - 1] == '\n'))
	len--;
    ports[len] = '\000';

    for ( /* param */ ; *ports != '\000'; ports = next) {
	if ((next = strchr(ports, ':')) == (char *)0)
	    next = "";
	else
	    *next++ = '\000';

	if ((server = strchr(ports, '@')) != (char *)0) {
	    *server++ = '\000';
	    if (*server == '\000')
		server = master;
	} else
	    server = master;

	if (*ports == '\000') {
	    port = htons(bindPort);
	} else if (!isdigit((int)(ports[0]))) {
	    Error("invalid port spec for %s: `%s'", server, ports);
	    continue;
	} else {
	    port = htons((short)atoi(ports));
	}

      attemptLogin:
	if ((pcf = GetPort(server, port)) == (CONSFILE *)0)
	    continue;

	t = ReadReply(pcf, 0);
	if (strcmp(t, "ok\r\n") != 0) {
	    FileClose(&pcf);
	    FilePrint(cfstdout, "%s: %s", server, t);
	    continue;
	}
#if HAVE_OPENSSL
	FileWrite(pcf, "ssl\r\n", 5);
	t = ReadReply(pcf, 0);
	if (strcmp(t, "ok\r\n") == 0) {
	    AttemptSSL(pcf);
	}
	if (fReqEncryption && FileGetType(pcf) != SSLSocket) {
	    Error("Encryption not supported by server `%s'", server);
	    FileClose(&pcf);
	    continue;
	}
#endif

	BuildTmpString((char *)0);
	BuildTmpString("login ");
	BuildTmpString(pcUser);
	t = BuildTmpString("\r\n");
	FileWrite(pcf, t, -1);

	t = ReadReply(pcf, 0);
	if (strcmp(t, "passwd?\r\n") == 0) {
	    static int count = 0;
	    static STRING *tmpString = (STRING *)0;
	    if (tmpString == (STRING *)0)
		tmpString = AllocString();
	    if (tmpString->used <= 1) {
		char *pass;
		sprintf(acMesg, "Enter %s@%s's password: ", pcUser,
			master);
		pass = GetPassword(acMesg);
		if (pass == (char *)0) {
		    Error("could not get password from tty for `%s'",
			  server);
		    FileClose(&pcf);
		    continue;
		}
		BuildString(pass, tmpString);
		BuildString("\r\n", tmpString);
	    }
	    FileWrite(pcf, tmpString->string, tmpString->used - 1);
	    t = ReadReply(pcf, 0);
	    if (strcmp(t, "ok\r\n") != 0) {
		FilePrint(cfstdout, "%s: %s", server, t);
		if (++count < 3) {
		    BuildString((char *)0, tmpString);
		    goto attemptLogin;
		}
		Error("too many bad passwords for `%s'", server);
		count = 0;
		FileClose(&pcf);
		continue;
	    } else
		count = 0;
	}

	/* now that we're logged in, we can do something */
	/* if we're on the last cmd or the command is 'call' and we
	 * have an arg (always true if it's 'call'), then send the arg
	 */
	if ((cmdi == 0 || cmds[cmdi][0] == 'c') && cmdarg != (char *)0)
	    FilePrint(pcf, "%s %s\r\n", cmds[cmdi], cmdarg);
	else
	    FilePrint(pcf, "%s\r\n", cmds[cmdi]);

	/* if we haven't gone down the stack, do "normal" stuff.
	 * if we did hit the bottom, we send the exit\r\n now so
	 * that the ReadReply can stop once the socket closes.
	 */
	if (cmdi != 0) {
	    t = ReadReply(pcf, 0);
	    /* save the result */
	    if ((result = strdup(t)) == (char *)0)
		OutOfMem();
	}

	/* if we're working on finding a console */
	if (cmds[cmdi][0] == 'c') {
	    /* did we get a redirect? */
	    if (result[0] == '@' || (result[0] >= '0' && result[0] <= '9')) {
		static int limit = 0;
		if (limit++ > 10) {
		    Error("forwarding level too deep!");
		    exit(EX_SOFTWARE);
		}
	    } else if (result[0] != '[') {	/* did we not get a connection? */
		FilePrint(cfstdout, "%s: %s", server, result);
		FileClose(&pcf);
		continue;
	    } else {
		/* right now, we can only connect to one console, so it's ok
		 * to clear the password.  if we were allowed to connect to
		 * multiple consoles (somehow), either in parallel or serial,
		 * we wouldn't want to do this here */
		ClearPassword();
		CallUp(pcf, server, cmdarg, cmds[0], result);
		return 0;
	    }
	} else if (cmds[cmdi][0] == 'q') {
	    t = ReadReply(pcf, 0);
	    FileWrite(cfstdout, t, -1);
	    if (t[0] != 'o' || t[1] != 'k') {
		FileWrite(pcf, "exit\r\n", 6);
		t = ReadReply(pcf, 1);
	    }
	} else {
	    /* all done */
	    FileWrite(pcf, "exit\r\n", 6);
	    t = ReadReply(pcf, cmdi == 0 ? 1 : 0);

	    if (cmdi == 0) {
		int len;
		/* if we hit bottom, this is where we get our results */
		if ((result = strdup(t)) == (char *)0)
		    OutOfMem();
		len = strlen(result);
		if (len > 8 &&
		    strcmp("goodbye\r\n", result + len - 9) == 0) {
		    len -= 9;
		    *(result + len) = '\000';
		}
		/* if (not 'broadcast' and not 'textmsg') or 
		 *   result doesn't start with 'ok' (only checks this if
		 *      it's a 'broadcast' or 'textmsg')
		 */
		if (cmds[0][0] == 'd') {
		    if (result[0] != 'o' || result[1] != 'k') {
			FileWrite(cfstdout, server, -1);
			FileWrite(cfstdout, ": ", 2);
			FileWrite(cfstdout, result, len);
		    } else {
			disconnectCount += atoi(result + 19);
		    }
		} else if ((cmds[0][0] != 'b' && cmds[0][0] != 't') ||
			   (result[0] != 'o' || result[1] != 'k')) {
		    /* did a 'master' before this or doing a 'disconnect' */
		    if (cmds[1][0] == 'm' || cmds[0][0] == 'd') {
			FileWrite(cfstdout, server, -1);
			FileWrite(cfstdout, ": ", 2);
		    }
		    FileWrite(cfstdout, result, len);
		}
	    }
	}

	FileClose(&pcf);

	/* this would only be true if we got extra redirects (@... above) */
	if (cmds[cmdi][0] == 'c')
	    DoCmds(server, result, cmdi);
	else if (cmdi > 0)
	    DoCmds(server, result, cmdi - 1);
	free(result);
    }

    return 0;
}


/* mainline for console client program					(ksb)
 * setup who we are, and what our loopback addr is
 * parse the cmd line,
 * (optionally) get a shutdown passwd
 * Gather results
 * exit happy or sad
 */
int
#if PROTOTYPES
main(int argc, char **argv)
#else
main(argc, argv)
    int argc;
    char **argv;
#endif
{
    char *pcCmd;
    struct passwd *pwdMe = (struct passwd *)0;
    int opt;
    int fLocal;
    static STRING *acPorts = (STRING *)0;
    static char acOpts[] = "7aAb:B:c:d:De:EfFhiIl:M:p:PqQrRsSt:uvVwWx";
    extern int optind;
    extern int optopt;
    extern char *optarg;
    int i;
    STRING *textMsg = (STRING *)0;
    int cmdi;
    int retval;

    isMultiProc = 0;		/* make sure stuff DOESN'T have the pid */

    if (textMsg == (STRING *)0)
	textMsg = AllocString();
    if (acPorts == (STRING *)0)
	acPorts = AllocString();

    if ((char *)0 == (progname = strrchr(argv[0], '/'))) {
	progname = argv[0];
    } else {
	++progname;
    }

    /* command line parsing
     */
    pcCmd = (char *)0;
    fLocal = 0;
    while ((opt = getopt(argc, argv, acOpts)) != EOF) {
	switch (opt) {
	    case '7':		/* strip high-bit */
		fStrip = 1;
		break;

	    case 'A':		/* attach with log replay */
		fReplay = 1;
		/* fall through */
	    case 'a':		/* attach */
		pcCmd = "attach";
		break;

	    case 'B':		/* broadcast message */
		fLocal = 1;
		/* fall through */
	    case 'b':
		pcCmd = "broadcast";
		cmdarg = optarg;
		break;

	    case 'c':
#if HAVE_OPENSSL
		pcCredFile = optarg;
#endif
		break;

	    case 'D':
		fDebug++;
		break;

	    case 'd':
		pcCmd = "disconnect";
		cmdarg = optarg;
		break;

	    case 'E':
#if HAVE_OPENSSL
		fReqEncryption = 0;
#endif
		break;

	    case 'e':		/* set escape chars */
		ParseEsc(optarg);
		break;

	    case 'F':		/* force attach with log replay */
		fReplay = 1;
		/* fall through */
	    case 'f':		/* force attach */
		pcCmd = "force";
		break;

	    case 'I':
		fLocal = 1;
		/* fall through */
	    case 'i':
		pcCmd = "info";
		break;

	    case 'l':
		pcUser = optarg;
		break;

	    case 'M':
		pcInMaster = optarg;
		break;

	    case 'p':
		pcPort = optarg;
		break;

	    case 'P':		/* send a pid command to the server     */
		pcCmd = "pid";
		break;

	    case 'Q':		/* only quit this host          */
		fLocal = 1;
		/*fallthough */
	    case 'q':		/* send quit command to server  */
		pcCmd = "quit";
		break;

	    case 'R':
		fLocal = 1;
		/*fallthrough */
	    case 'r':		/* display daemon version */
		pcCmd = "version";
		break;

	    case 'S':		/* spy with log replay */
		fReplay = 1;
		/* fall through */
	    case 's':		/* spy */
		pcCmd = "spy";
		break;

	    case 't':
		BuildString((char *)0, textMsg);
		if (optarg == (char *)0 || *optarg == '\000') {
		    Error("no destination specified for -t", optarg);
		    exit(EX_UNAVAILABLE);
		} else if (strchr(optarg, ' ') != (char *)0) {
		    Error("-t option cannot contain a space: `%s'",
			  optarg);
		    exit(EX_UNAVAILABLE);
		}
		BuildString("textmsg ", textMsg);
		BuildString(optarg, textMsg);
		pcCmd = textMsg->string;
		break;

	    case 'u':
		pcCmd = "hosts";
		break;

	    case 'W':
		fLocal = 1;
		/*fallthrough */
	    case 'w':		/* who */
		pcCmd = "group";
		break;

	    case 'x':
		pcCmd = "examine";
		break;

	    case 'v':
		fVerbose = 1;
		break;

	    case 'V':
		fVersion = 1;
		break;

	    case 'h':		/* huh? */
		Usage(1);
		exit(EX_OK);

	    case '\?':		/* huh? */
		Usage(0);
		exit(EX_UNAVAILABLE);

	    default:
		Error("option %c needs a parameter", optopt);
		exit(EX_UNAVAILABLE);
	}
    }

    if (fVersion) {
	Version();
	exit(EX_OK);
    }

    /* finish resolving the command to do */
    if (pcCmd == (char *)0) {
	pcCmd = "attach";
    }

    if (*pcCmd == 'a' || *pcCmd == 'f' || *pcCmd == 's') {
	if (optind >= argc) {
	    Error("missing console name");
	    exit(EX_UNAVAILABLE);
	}
	cmdarg = argv[optind++];
    } else if (*pcCmd == 't') {
	if (optind >= argc) {
	    Error("missing message text");
	    exit(EX_UNAVAILABLE);
	}
	cmdarg = argv[optind++];
    }

    if (optind < argc) {
	Error("extra garbage on command line? (%s...)", argv[optind]);
	exit(EX_UNAVAILABLE);
    }

    /* if we somehow lost the port (or got an empty string), reset */
    if (pcPort == (char *)0 || pcPort[0] == '\000')
	pcPort = DEFPORT;

    /* Look for non-numeric characters */
    for (i = 0; pcPort[i] != '\000'; i++)
	if (!isdigit((int)pcPort[i]))
	    break;

    if (pcPort[i] == '\000') {
	/* numeric only */
	bindPort = atoi(pcPort);
    } else {
	/* non-numeric only */
	struct servent *pSE;
	if ((pSE = getservbyname(pcPort, "tcp")) == (struct servent *)0) {
	    Error("getservbyname(%s): %s", pcPort, strerror(errno));
	    exit(EX_UNAVAILABLE);
	} else {
	    bindPort = ntohs((u_short) pSE->s_port);
	}
    }

    if (pcUser == (char *)0 || pcUser[0] == '\000') {
	if (((pcUser = getenv("LOGNAME")) == (char *)0) &&
	    ((pcUser = getenv("USER")) == (char *)0) &&
	    ((pwdMe = getpwuid(getuid())) == (struct passwd *)0)) {
	    Error
		("$LOGNAME and $USER do not exist and getpwuid fails: %d: %s",
		 (int)(getuid()), strerror(errno));
	    exit(EX_UNAVAILABLE);
	}
	if (pcUser == (char *)0) {
	    if (pwdMe->pw_name == (char *)0 || pwdMe->pw_name[0] == '\000') {
		Error("Username for uid %d does not exist",
		      (int)(getuid()));
		exit(EX_UNAVAILABLE);
	    } else {
		pcUser = pwdMe->pw_name;
	    }
	}
    }


    SimpleSignal(SIGPIPE, SIG_IGN);

    cfstdout = FileOpenFD(1, simpleFile);

    BuildString((char *)0, acPorts);
    BuildStringChar('@', acPorts);
    BuildString(pcInMaster, acPorts);

#if HAVE_OPENSSL
    SetupSSL();			/* should only do if we want ssl - provide flag! */
#endif

    /* stack up the commands for DoCmds() */
    cmdi = -1;
    cmds[++cmdi] = pcCmd;

    if (*pcCmd == 'q' || *pcCmd == 'v' || *pcCmd == 'p') {
	if (!fLocal)
	    cmds[++cmdi] = "master";
    } else if (*pcCmd == 'a' || *pcCmd == 'f' || *pcCmd == 's') {
	ValidateEsc();
	cmds[++cmdi] = "call";
    } else {
	cmds[++cmdi] = "groups";
	if (!fLocal)
	    cmds[++cmdi] = "master";
    }

    retval = DoCmds(pcInMaster, acPorts->string, cmdi);

    if (*pcCmd == 'd')
	FilePrint(cfstdout, "Disconnected %d users\n", disconnectCount);

    exit(retval);
}
