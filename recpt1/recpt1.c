#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <ctype.h>
#include <libgen.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/ioctl.h>
#include "pt1_ioctl.h"

#include "recpt1.h"
#include "decoder.h"
#include "version.h"
#include "mkpath.h"

/* maximum write length at once */
#define SIZE_CHANK 1316

/* globals */
int f_exit = FALSE;

/* type definitions */
typedef struct sock_data {
    int sfd;    /* socket fd */
    struct sockaddr_in addr;
} sock_data;

typedef struct reader_thread_data {
    QUEUE_T *queue;
    decoder *decoder;
    int wfd;    /* output file fd */
    sock_data *sock_data;
    pthread_t signal_thread;
} reader_thread_data;

typedef struct signal_thread_data {
    QUEUE_T *queue;
    int tfd;    /* tuner fd */
} signal_thread_data;

/* lookup frequency conversion table*/
ISDB_T_FREQ_CONV_TABLE *
searchrecoff(char *channel)
{
    int lp;

    for(lp = 0; isdb_t_conv_table[lp].parm_freq != NULL; lp++) {
        /* return entry number in the table when strings match and
         * lengths are same. */
        if((memcmp(isdb_t_conv_table[lp].parm_freq, channel,
                   strlen(channel)) == 0) &&
           (strlen(channel) == strlen(isdb_t_conv_table[lp].parm_freq))) {
            return &isdb_t_conv_table[lp];
        }
    }
    return NULL;
}

QUEUE_T *
create_queue(size_t size)
{
    QUEUE_T *p_queue;
    int memsize = sizeof(QUEUE_T) + size * sizeof(BUFSZ);

    p_queue = (QUEUE_T*)calloc(memsize, sizeof(char));

    if(p_queue != NULL) {
        p_queue->size = size;
        p_queue->num_avail = size;
        p_queue->num_used = 0;
        pthread_mutex_init(&p_queue->mutex, NULL);
        pthread_cond_init(&p_queue->cond_avail, NULL);
        pthread_cond_init(&p_queue->cond_used, NULL);
    }

    return p_queue;
}

void
destroy_queue(QUEUE_T *p_queue)
{
    if(!p_queue)
        return;

    pthread_mutex_destroy(&p_queue->mutex);
    pthread_cond_destroy(&p_queue->cond_avail);
    pthread_cond_destroy(&p_queue->cond_used);
    free(p_queue);
}

/* enqueue data. this function will block if queue is full. */
void
enqueue(QUEUE_T *p_queue, BUFSZ *data)
{
    struct timeval now;
    struct timespec spec;

    gettimeofday(&now, NULL);
    spec.tv_sec = now.tv_sec + 1;
    spec.tv_nsec = now.tv_usec * 1000;

    pthread_mutex_lock(&p_queue->mutex);
    /* entered critical section */

    /* wait while queue is full */
    while(p_queue->num_avail == 0) {
        pthread_cond_timedwait(&p_queue->cond_avail,
                               &p_queue->mutex, &spec);
        if(f_exit) {
            pthread_mutex_unlock(&p_queue->mutex);
            return;
        }
    }

    p_queue->buffer[p_queue->in] = data;

    /* move position marker for input to next position */
    p_queue->in++;
    p_queue->in %= p_queue->size;

    /* update counters */
    p_queue->num_avail--;
    p_queue->num_used++;

    /* leaving critical section */
    pthread_mutex_unlock(&p_queue->mutex);
    pthread_cond_signal(&p_queue->cond_used);
}

/* dequeue data. this function will block if queue is empty. */
BUFSZ *
dequeue(QUEUE_T *p_queue)
{
    struct timeval now;
    struct timespec spec;
    BUFSZ *buffer;

    gettimeofday(&now, NULL);
    spec.tv_sec = now.tv_sec + 1;
    spec.tv_nsec = now.tv_usec * 1000;

    pthread_mutex_lock(&p_queue->mutex);
    /* entered the critical section*/

    /* wait while queue is empty */
    while(p_queue->num_used == 0) {
        pthread_cond_timedwait(&p_queue->cond_used,
                               &p_queue->mutex, &spec);
        if(f_exit) {
            pthread_mutex_unlock(&p_queue->mutex);
            return NULL;
        }
    }

    /* take buffer address */
    buffer = p_queue->buffer[p_queue->out];

    /* move position marker for output to next position */
    p_queue->out++;
    p_queue->out %= p_queue->size;

    /* update counters */
    p_queue->num_avail++;
    p_queue->num_used--;

    /* leaving the critical section */
    pthread_mutex_unlock(&p_queue->mutex);
    pthread_cond_signal(&p_queue->cond_avail);

    return buffer;
}

/* this function will be reader thread */
void *
reader_func(void *p)
{
    reader_thread_data *data = (reader_thread_data *)p;
    QUEUE_T *p_queue = data->queue;
    decoder *dec = data->decoder;
    int wfd = data->wfd;
    int use_b25 = dec ? TRUE : FALSE;
    int use_udp = data->sock_data ? TRUE : FALSE;
    int fileless = FALSE;
    int sfd = -1;
    pthread_t signal_thread = data->signal_thread;
    struct sockaddr_in *addr = NULL;
    BUFSZ *qbuf;
    ARIB_STD_B25_BUFFER sbuf, dbuf, buf;
    int code;

    if(wfd == -1)
        fileless = TRUE;

    if(use_udp) {
        sfd = data->sock_data->sfd;
        addr = &data->sock_data->addr;
    }

    while(1) {
        ssize_t wc = 0;
        int file_err = 0;
        qbuf = dequeue(p_queue);
        /* no entry in the queue */
        if(qbuf == NULL) {
            break;
        }

        sbuf.data = qbuf->buffer;
        sbuf.size = qbuf->size;

        buf = sbuf; /* default */

        if(use_b25) {
            code = b25_decode(dec, &sbuf, &dbuf);
            if(code < 0)
                fprintf(stderr, "b25_decode failed\n");
            else
                buf = dbuf;
        }

        if(!fileless) {
            /* write data to output file */
            int size_remain = buf.size;
            int offset = 0;
            while(size_remain > 0) {
                int ws = size_remain < SIZE_CHANK ? size_remain : SIZE_CHANK;
                wc = write(wfd, buf.data + offset, ws);
                if(wc < 0) {
                    perror("write");
                    file_err = 1;
                    pthread_kill(signal_thread,
                                 errno == EPIPE ? SIGPIPE : SIGUSR2);
                    break;
                }
                size_remain -= wc;
                offset += wc;
            }
        }

        if(use_udp && sfd != -1) {
            /* write data to socket */
            int size_remain = buf.size;
            int offset = 0;
            while(size_remain > 0) {
                int ws = size_remain < SIZE_CHANK ? size_remain : SIZE_CHANK;
                wc = write(sfd, buf.data + offset, ws);
                if(wc < 0) {
                    if(errno == EPIPE)
                        pthread_kill(signal_thread, SIGPIPE);
                    break;
                }
                size_remain -= wc;
                offset += wc;
            }
        }

        free(qbuf);

        /* normal exit */
        if(f_exit && !p_queue->num_used && !file_err) {

            buf = sbuf; /* default */

            if(use_b25) {
                code = b25_finish(dec, &sbuf, &dbuf);
                if(code < 0)
                    fprintf(stderr, "b25_finish failed\n");
                else
                    buf = dbuf;
            }

            if(!fileless) {
                wc = write(wfd, buf.data, buf.size);
                if(wc < 0) {
                    perror("write");
                    file_err = 1;
                    pthread_kill(signal_thread,
                                 errno == EPIPE ? SIGPIPE : SIGUSR2);
                }
            }

            if(use_udp && sfd != -1) {
                wc = write(sfd, buf.data, buf.size);
                if(wc < 0) {
                    if(errno == EPIPE)
                        pthread_kill(signal_thread, SIGPIPE);
                }
            }

            break;
        }
    }

    return NULL;
}

void
show_usage(char *cmd)
{
    fprintf(stderr, "Usage: \n%s [--b25 [--round N] [--strip] [--EMM]] [--udp [--addr hostname --port portnumber]] [--device devicefile] channel rectime destfile\n", cmd);
    fprintf(stderr, "\n");
    fprintf(stderr, "Remarks:\n");
    fprintf(stderr, "if rectime  is '-', records indefinitely.\n");
    fprintf(stderr, "if destfile is '-', stdout is used for output.\n");
}

void
show_options(void)
{
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "--b25:               Decrypt using BCAS card\n");
    fprintf(stderr, "  --round N:         Specify round number\n");
    fprintf(stderr, "  --strip:           Strip null stream\n");
    fprintf(stderr, "  --EMM:             Instruct EMM operation\n");
    fprintf(stderr, "--udp:               Turn on udp broadcasting\n");
    fprintf(stderr, "  --addr hostname:   Hostname or address to connect\n");
    fprintf(stderr, "  --port portnumber: Port number to connect\n");
    fprintf(stderr, "--device devicefile: Specify devicefile to use\n");
    fprintf(stderr, "--help:              Show this help\n");
    fprintf(stderr, "--version:           Show version\n");
    fprintf(stderr, "--list:              Show channel list\n");
}
void
show_channels(void)
{
    FILE *f;
    char *home;
    char buf[255], filename[255];

    fprintf(stderr, "Available Channels:\n");

    home = getenv("HOME");
    sprintf(filename, "%s/.recpt1-channels", home);
    f = fopen(filename, "r");
    if(f) {
        while(fgets(buf, 255, f))
            fprintf(stderr, "%s", buf);
        fclose(f);
    }
    else
        fprintf(stderr, "13-62: Terrestrial Channels\n");

    fprintf(stderr, "101ch: NHK BS1\n");
    fprintf(stderr, "102ch: NHK BS2\n");
    fprintf(stderr, "103ch: NHK BShi\n");
    fprintf(stderr, "141ch: BS Nittele\n");
    fprintf(stderr, "151ch: BS Asahi\n");
    fprintf(stderr, "161ch: BS-TBS\n");
    fprintf(stderr, "171ch: BS Japan\n");
    fprintf(stderr, "181ch: BS Fuji\n");
    fprintf(stderr, "191ch: WOWOW\n");
    fprintf(stderr, "200ch: Star Channel\n");
    fprintf(stderr, "211ch: BS11 Digital\n");
    fprintf(stderr, "222ch: TwellV\n");
    fprintf(stderr, "CS2-CS24: CS Channels\n");
}

float
getsignal_isdb_s(int signal)
{
    /* apply linear interpolation */
    static const float afLevelTable[] = {
        24.07f,    // 00    00    0        24.07dB
        24.07f,    // 10    00    4096     24.07dB
        18.61f,    // 20    00    8192     18.61dB
        15.21f,    // 30    00    12288    15.21dB
        12.50f,    // 40    00    16384    12.50dB
        10.19f,    // 50    00    20480    10.19dB
        8.140f,    // 60    00    24576    8.140dB
        6.270f,    // 70    00    28672    6.270dB
        4.550f,    // 80    00    32768    4.550dB
        3.730f,    // 88    00    34816    3.730dB
        3.630f,    // 88    FF    35071    3.630dB
        2.940f,    // 90    00    36864    2.940dB
        1.420f,    // A0    00    40960    1.420dB
        0.000f     // B0    00    45056    -0.01dB
    };

    unsigned char sigbuf[4];
    memset(sigbuf, '\0', sizeof(sigbuf));
    sigbuf[0] =  (((signal & 0xFF00) >> 8) & 0XFF);
    sigbuf[1] =  (signal & 0xFF);

    /* calculate signal level */
    if(sigbuf[0] <= 0x10U) {
        /* clipped maximum */
        return 24.07f;
    }
    else if (sigbuf[0] >= 0xB0U) {
        /* clipped minimum */
        return 0.0f;
    }
    else {
        /* linear interpolation */
        const float fMixRate =
            (float)(((unsigned short)(sigbuf[0] & 0x0FU) << 8) |
                    (unsigned short)sigbuf[0]) / 4096.0f;
        return afLevelTable[sigbuf[0] >> 4] * (1.0f - fMixRate) +
            afLevelTable[(sigbuf[0] >> 4) + 0x01U] * fMixRate;
    }
}

void
calc_cn(int fd, int type)
{
    int     rc ;
    double  P ;
    double  CNR;

    if(ioctl(fd, GET_SIGNAL_STRENGTH, &rc) < 0) {
        fprintf(stderr, "Tuner Select Error\n");
        return ;
    }

    if(type == CHTYPE_GROUND) {
        P = log10(5505024/(double)rc) * 10;
        CNR = (0.000024 * P * P * P * P) - (0.0016 * P * P * P) +
                    (0.0398 * P * P) + (0.5491 * P)+3.0965;
        fprintf(stderr, "Signal=%fdB\n", CNR);
    }
    else {
        CNR = getsignal_isdb_s(rc);
        fprintf(stderr, "Signal=%fdB\n", CNR);
    }
}

void
cleanup(signal_thread_data *sdata)
{
    /* stop recording */
    ioctl(sdata->tfd, STOP_REC, 0);

    /* restore LNB state */
#if 0
    if(ptr->type == CHTYPE_SATELLITE) {
        if(ioctl(tfd, LNB_DISABLE, 0) < 0) {
            return 0 ;
        }
    }
#endif
    /* xxx need mutex? */
    f_exit = TRUE;

    pthread_cond_signal(&sdata->queue->cond_avail);
    pthread_cond_signal(&sdata->queue->cond_used);
}

/* will be signal handler thread */
void *
process_signals(void *data)
{
    sigset_t waitset;
    int sig;
    signal_thread_data *sdata;
    sdata = (signal_thread_data *)data;

    sigemptyset(&waitset);
    sigaddset(&waitset, SIGPIPE);
    sigaddset(&waitset, SIGINT);
    sigaddset(&waitset, SIGTERM);
    sigaddset(&waitset, SIGUSR1);
    sigaddset(&waitset, SIGUSR2);

    sigwait(&waitset, &sig);

    switch(sig) {
    case SIGPIPE:
        fprintf(stderr, "\nSIGPIPE received. cleaning up...\n");
        cleanup(sdata);
        break;
    case SIGINT:
        fprintf(stderr, "\nSIGINT received. cleaning up...\n");
        cleanup(sdata);
        break;
    case SIGTERM:
        fprintf(stderr, "\nSIGTERM received. cleaning up...\n");
        cleanup(sdata);
        break;
    case SIGUSR1: /* normal exit*/
        cleanup(sdata);
        break;
    case SIGUSR2: /* error */
        fprintf(stderr, "Detected an error. cleaning up...\n");
        cleanup(sdata);
        break;
    }

    return NULL; /* dummy */
}

void
init_signal_handlers(pthread_t *signal_thread, signal_thread_data *sdata)
{
    sigset_t blockset;

    sigemptyset(&blockset);
    sigaddset(&blockset, SIGPIPE);
    sigaddset(&blockset, SIGINT);
    sigaddset(&blockset, SIGTERM);
    sigaddset(&blockset, SIGUSR1);
    sigaddset(&blockset, SIGUSR2);

    if(pthread_sigmask(SIG_BLOCK, &blockset, NULL))
        fprintf(stderr, "pthread_sigmask() failed.\n");

    pthread_create(signal_thread, NULL, process_signals, sdata);
}

int
main(int argc, char **argv)
{
    int tfd, wfd;
    int lp;
    int recsec = 0;
    int indefinite = FALSE;
    time_t start_time, cur_time;
    FREQUENCY freq;
    ISDB_T_FREQ_CONV_TABLE *ptr;
    pthread_t reader_thread;
    pthread_t signal_thread;
    QUEUE_T *p_queue = create_queue(MAX_QUEUE);
    BUFSZ   *bufptr;
    decoder *dec = NULL;
    static reader_thread_data tdata;
    static signal_thread_data sdata;
    decoder_options dopt = {
        4,  /* round */
        0,  /* strip */
        0   /* emm */
    };

    int result;
    int option_index;
    struct option long_options[] = {
        { "b25",       0, NULL, 'b'},
        { "B25",       0, NULL, 'b'},
        { "round",     1, NULL, 'r'},
        { "strip",     0, NULL, 's'},
        { "emm",       0, NULL, 'm'},
        { "EMM",       0, NULL, 'm'},
        { "udp",       0, NULL, 'u'},
        { "addr",      1, NULL, 'a'},
        { "port",      1, NULL, 'p'},
        { "device",    1, NULL, 'd'},
        { "help",      0, NULL, 'h'},
        { "version",   0, NULL, 'v'},
        { "list",      0, NULL, 'l'},
        {0, 0, NULL, 0} /* terminate */
    };

    int use_b25 = FALSE;
    int use_udp = FALSE;
    int fileless = FALSE;
    int use_stdout = FALSE;
    char *host_to = NULL;
    int port_to = 1234;
    sock_data *sockdata = NULL;
    char *device = NULL;
    char **tuner;
    int num_devs;

    while((result = getopt_long(argc, argv, "br:smua:p:d:hvl",
                                long_options, &option_index)) != -1) {
        switch(result) {
        case 'b':
            use_b25 = TRUE;
            fprintf(stderr, "using B25...\n");
            break;
        case 's':
            dopt.strip = TRUE;
            fprintf(stderr, "enable B25 strip\n");
            break;
        case 'm':
            dopt.emm = TRUE;
            fprintf(stderr, "enable B25 emm processing\n");
            break;
        case 'u':
            use_udp = TRUE;
            host_to = "localhost";
            fprintf(stderr, "enable UDP broadcasting\n");
            break;
        case 'h':
            fprintf(stderr, "\n");
            show_usage(argv[0]);
            fprintf(stderr, "\n");
            show_options();
            fprintf(stderr, "\n");
            show_channels();
            fprintf(stderr, "\n");
            exit(0);
            break;
        case 'v':
            fprintf(stderr, "%s %s\n", argv[0], version);
            fprintf(stderr, "recorder command for PT1 digital tuner.\n");
            exit(0);
            break;
        case 'l':
            show_channels();
            exit(0);
            break;
        /* following options require argument */
        case 'r':
            dopt.round = atoi(optarg);
            fprintf(stderr, "set round %d\n", dopt.round);
            break;
        case 'a':
            use_udp = TRUE;
            host_to = optarg;
            fprintf(stderr, "UDP destination address: %s\n", host_to);
            break;
        case 'p':
            port_to = atoi(optarg);
            fprintf(stderr, "UDP port: %d\n", port_to);
            break;
        case 'd':
            device = optarg;
            fprintf(stderr, "using device: %s\n", device);
            break;
        }
    }

    if(argc - optind < 3) {
        if(argc - optind == 2 && use_udp) {
            fprintf(stderr, "Fileless UDP broadcasting\n");
            fileless = TRUE;
            wfd = -1;
        }
        else {
            fprintf(stderr, "Arguments are necessary!\n");
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return 1;
        }
    }

    /* get channel */
    ptr = searchrecoff(argv[optind]);
    if(ptr == NULL) {
        fprintf(stderr, "Invalid Channel: %s\n", argv[optind]);
        return 1;
    }

    freq.frequencyno = ptr->set_freq;
    freq.slot = ptr->add_freq;

    /* open tuner */
    /* case 1: specified tuner device */
    if(device) {
        tfd = open(device, O_RDONLY);
        if(tfd < 0) {
            fprintf(stderr, "Cannot open tuner device: %s\n", device);
            return 1;
        }

        /* power on LNB */
        if(ptr->type == CHTYPE_SATELLITE) {
            if(ioctl(tfd, LNB_ENABLE, 0) < 0) {
                close(tfd);
                fprintf(stderr, "Power on LNB failed: %s\n", device);
                return 1;
            }
        }

        /* tune to specified channel */
        if(ioctl(tfd, SET_CHANNEL, &freq) < 0) {
            close(tfd);
            fprintf(stderr, "Cannot tune to the specified channel: %s\n", device);
            return 1;
        }
    }
    else {
        /* case 2: loop around available devices */
        if(ptr->type == CHTYPE_SATELLITE) {
            tuner = bsdev;
            num_devs = NUM_BSDEV;
        }
        else {
            tuner = isdb_t_dev;
            num_devs = NUM_ISDB_T_DEV;
        }

        for(lp = 0; lp < num_devs; lp++) {
            tfd = open(tuner[lp], O_RDONLY);
            if(tfd >= 0) {
                /* power on LNB */
                if(ptr->type == CHTYPE_SATELLITE) {
                    if(ioctl(tfd, LNB_ENABLE, 0) < 0) {
                        close(tfd);
                        tfd = -1;
                        continue;
                    }
                }

                /* tune to specified channel */
                if(ioctl(tfd, SET_CHANNEL, &freq) < 0) {
                    close(tfd);
                    tfd = -1;
                    continue;
                }

                break; /* found suitable tuner */
            }
        }

        /* all tuners cannot be used */
        if(tfd < 0) {
            fprintf(stderr, "Cannot tune to the specified channel\n");
            return 1;
        }
    }

    /* show signal strength */
    calc_cn(tfd, ptr->type);

    /* get recsec */
    char *rectimestr = argv[optind + 1];

    /* indefinite */
    if(!strcmp("-", rectimestr)) {
        indefinite = TRUE;
        recsec = -1;
    }
    /* colon */
    else if(strchr(rectimestr, ':')) {
        int n1, n2, n3;
        if(sscanf(rectimestr, "%d:%d:%d", &n1, &n2, &n3) == 3)
            recsec = n1 * 3600 + n2 * 60 + n3;
        else if(sscanf(rectimestr, "%d:%d", &n1, &n2) == 2)
            recsec = n1 * 3600 + n2 * 60;
    }
    /* HMS */
    else {
        char *tmpstr;
        char *p1, *p2;

        tmpstr = strdup(rectimestr);
        p1 = tmpstr;
        while(*p1 && !isdigit(*p1))
            p1++;

        /* hour */
        if((p2 = strchr(p1, 'H')) || (p2 = strchr(p1, 'h'))) {
            *p2 = '\0';
            recsec += atoi(p1) * 3600;
            p1 = p2 + 1;
            while(*p1 && !isdigit(*p1))
                p1++;
        }

        /* minute */
        if((p2 = strchr(p1, 'M')) || (p2 = strchr(p1, 'm'))) {
            *p2 = '\0';
            recsec += atoi(p1) * 60;
            p1 = p2 + 1;
            while(*p1 && !isdigit(*p1))
                p1++;
        }

        /* second */
        recsec += atoi(p1);

        free(tmpstr);
    }

    /* fprintf(stderr, "recsec = %d\n", recsec); */

    /* open output file */
    char *destfile = argv[optind + 2];
    if(destfile && !strcmp("-", destfile)) {
        use_stdout = TRUE;
        wfd = 1; /* stdout */
    }
    else {
        if(!fileless) {
            int status;
            char *path = strdup(argv[optind + 2]);
            char *dir = dirname(path);
            status = mkpath(dir, 0777);
            if(status == -1)
                perror("mkpath");
            free(path);

            wfd = open(argv[optind + 2], (O_RDWR | O_CREAT | O_TRUNC), 0666);
            if(wfd < 0) {
                fprintf(stderr, "Cannot open output file: %s\n",
                        argv[optind + 2]);
                return 1;
            }
        }
    }

    /* initialize decoder */
    if(use_b25) {
        dec = b25_startup(&dopt);
        if(!dec) {
            fprintf(stderr, "Cannot start b25 decoder\n");
            fprintf(stderr, "Fall back to encrypted recording\n");
            use_b25 = 0;
        }
    }

    /* initialize udp connection */
    if(use_udp) {
      sockdata = calloc(1, sizeof(sock_data));
      struct in_addr ia;
      ia.s_addr = inet_addr(host_to);
      if(ia.s_addr == INADDR_NONE) {
            struct hostent *hoste = gethostbyname(host_to);
            if(!hoste) {
                perror("gethostbyname");
                return 1;
            }
            ia.s_addr = *(in_addr_t*) (hoste->h_addr_list[0]);
        }
        if((sockdata->sfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("socket");
            return 1;
        }

        sockdata->addr.sin_family = AF_INET;
        sockdata->addr.sin_port = htons (port_to);
        sockdata->addr.sin_addr.s_addr = ia.s_addr;

        if(connect(sockdata->sfd, (struct sockaddr *)&sockdata->addr,
                   sizeof(sockdata->addr)) < 0) {
            perror("connect");
            return 1;
        }
    }

    /* spawn signal handler thread */
    sdata.queue = p_queue;
    sdata.tfd = tfd;
    init_signal_handlers(&signal_thread, &sdata);

    /* spawn reader thread */
    tdata.queue = p_queue;
    tdata.decoder = dec;
    tdata.wfd = wfd;
    tdata.sock_data = sockdata;
    tdata.signal_thread = signal_thread;
    pthread_create(&reader_thread, NULL, reader_func, &tdata);

    /* start recording */
    if(ioctl(tfd, START_REC, 0) < 0) {
        fprintf(stderr, "Tuner cannot start recording\n");
        return 1;
    }

    fprintf(stderr, "Recording...\n");

    time(&start_time);

    /* read from tuner */
    while(1) {
        if(f_exit)
            break;

        time(&cur_time);
        bufptr = malloc(sizeof(BUFSZ));
        bufptr->size = read(tfd, bufptr->buffer, MAX_READ_SIZE);
        if(bufptr->size <= 0) {
            if((cur_time - start_time) >= recsec && !indefinite) {
                f_exit = TRUE;
                enqueue(p_queue, NULL);
                break;
            }
            else {
                continue;
            }
        }
        enqueue(p_queue, bufptr);

        /* stop recording */
        if((cur_time - start_time) >= recsec && !indefinite) {
            ioctl(tfd, STOP_REC, 0);
            /* read remaining data */
            while(1) {
                bufptr = malloc(sizeof(BUFSZ));
                bufptr->size = read(tfd, bufptr->buffer, MAX_READ_SIZE);
                if(bufptr->size <= 0) {
                    f_exit = TRUE;
                    enqueue(p_queue, NULL);
                    break;
                }
                enqueue(p_queue, bufptr);
            }
            break;
        }
    }

    pthread_kill(signal_thread, SIGUSR1);

    /* wait for threads */
    pthread_join(reader_thread, NULL);
    pthread_join(signal_thread, NULL);

    /* close tuner */
    if(ptr->type == CHTYPE_SATELLITE) {
        if(ioctl(tfd, LNB_DISABLE, 0) < 0) {
            return 0 ;
        }
    }
    close(tfd);

    /* release queue */
    destroy_queue(p_queue);

    /* close output file */
    if(!use_stdout)
        close(wfd);

    /* free socket data */
    if(use_udp) {
        close(sockdata->sfd);
        free(sockdata);
    }

    /* release decoder */
    if(use_b25) {
        b25_shutdown(dec);
    }

    return 0;
}
