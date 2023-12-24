#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <termios.h>
#include <assert.h>

#include <event2/event.h>
#include <event2/util.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#define MAX_MTU 9000

const char *defualt_out_addr = "127.0.0.1:14600";
const char *default_in_addr = "127.0.0.1:14601";



struct bufferevent *serial_bev;
struct sockaddr_in sin_out = {
	.sin_family = AF_INET,
};
struct sockaddr_in sin_out2 = {
	.sin_family = AF_INET,
};
int out_sock;
int out_sock2;
int metrics;

static void print_usage()
{
	printf("Usage: udpfwd [OPTIONS]\n"
	       "Where:\n"
	       "  --out           Remote output UDP port (%s by default)\n"
		   "  --out2          Second output UDP port (no default)\n"
	       "  --in            Remote input port (%s by default)\n"
		   "  --metrics       Display traffic info\n"
	       "  --help          Display this help\n",
		   " example: udpfwd --out 127.0.0.1:5600 --out2 127.0.0.1:5601 --in 192.168.1.8:5666 \n"
		   ,
	        defualt_out_addr, 
	       default_in_addr);
}
 

static bool parse_host_port(const char *s, struct in_addr *out_addr,
			    in_port_t *out_port)
{
	char host_and_port[32] = { 0 };
	strncpy(host_and_port, s, sizeof(host_and_port) - 1);

	char *colon = strchr(host_and_port, ':');
	if (NULL == colon) {
		return -1;
	}

	*colon = '\0';
	const char *host = host_and_port, *port_ptr = colon + 1;

	const bool is_valid_addr = inet_aton(host, out_addr) != 0;
	if (!is_valid_addr) {
		printf("Cannot parse host `%s'.\n", host);
		return false;
	}

	int port;
	if (sscanf(port_ptr, "%d", &port) != 1) {
		printf("Cannot parse port `%s'.\n", port_ptr);
		return false;
	}
	*out_port = htons(port);

	return true;
}

static void signal_cb(evutil_socket_t fd, short event, void *arg)
{
	struct event_base *base = arg;
	(void)event;

	printf("%s signal received\n", strsignal(fd));
	event_base_loopbreak(base);
}
 
static void serial_event_cb(struct bufferevent *bev, short events, void *arg)
{
	(void)bev;
	struct event_base *base = arg;

	if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
		printf("Serial connection closed\n");
		event_base_loopbreak(base);
	}
}
static long ttl_udp=0;

static long ttl_packs=0;

int max_udp_size;int max_udp_size;

static int slots = 10;

unsigned long long get_current_time_msWrong() {
    time_t current_time = time(NULL);
    return (unsigned long long)(current_time * 1000);
}

long long get_current_time_ms() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    // printf("milliseconds: %lld\n", milliseconds);
    return milliseconds;
}


long long start;
uint8_t last_slot;
static float scale=0;
//uint8_t SAMPLE_PER_SECOND=20;
//uint32_t perfs[SAMPLE_PER_SECOND];
uint32_t last_perf;
static uint32_t maxperf=0;
#define chart_width 50


void periodstats(long bytesread){
	long long elapsed=get_current_time_ms()-start;
	if (elapsed>=1000){//start new
		start=get_current_time_ms();
		elapsed=0;		
		
		float newscale=(float)maxperf/chart_width;
		if (newscale>scale*1.5 || newscale<scale/1.5){
			scale=newscale;
			printf("SCALE:%f",scale);
			for (uint8_t j = 0; j < chart_width; ++j) 
	            	printf("=");  
			printf("\n",scale);
		}
		maxperf=0;	
		//return;	
	}
	
	int slot=(elapsed/ (1000/metrics) );
	if (slot!=last_slot && scale>0){
		printf("%02d=%04d|", (slot*1000/metrics)/10, last_perf);	
		if (last_perf/scale>chart_width*2)//just in case not to overlap
			last_perf=scale * chart_width*2;
        for (uint8_t j = 0; j < (last_perf/scale); ++j) 
	       	 printf("%c", (char)254u);//printf("_");        				
		printf("\n");
		
		last_perf=0;
	}
	last_slot=slot;
	last_perf+=1;
	if(last_perf>maxperf)
			maxperf=last_perf;
	//perfs[slot]+=1;//bytesread;
	


}

static void in_read(evutil_socket_t sock, short event, void *arg)
{
	(void)event;
	unsigned char buf[MAX_MTU];
	struct event_base *base = arg;
	ssize_t nread;

	nread = recvfrom(sock, &buf, sizeof(buf) - 1, 0, NULL, NULL);
	if (nread == -1) {
		perror("recvfrom()");
		event_base_loopbreak(base);
	}

	if (sendto(out_sock, &buf, nread, 0,
			   (struct sockaddr *)&sin_out,
			   sizeof(sin_out)) == -1) {
			perror("sendto()");
			event_base_loopbreak(base);
		}

	if (sendto(out_sock2, &buf, nread, 0,
			   (struct sockaddr *)&sin_out2,
			   sizeof(sin_out2)) == -1) {
			perror("sendto2()");
			event_base_loopbreak(base);
		}		

	ttl_udp+=nread;
	ttl_packs++;

	if (metrics>0)
		periodstats(nread);
	else
		if (ttl_packs%1000==1)
 			printf("%d KB (%d) \n", ttl_udp/1024, ttl_udp/ttl_packs);

	

	//evbuffer_drain(&buf, nread);

	//assert(nread > 6);

	//dump_mavlink_packet(buf, "<<");

	//bufferevent_write(serial_bev, buf, nread);
}

static int handle_data(const char *port_name, int baudrate,
		       const char *out_addr,const char *out_addr2, const char *in_addr)
{
	struct event_base *base = NULL;
	struct event *sig_int = NULL, *in_ev = NULL;
	int ret = EXIT_SUCCESS;

	out_sock = socket(AF_INET, SOCK_DGRAM, 0);
	out_sock2 = socket(AF_INET, SOCK_DGRAM, 0);
	int in_sock = socket(AF_INET, SOCK_DGRAM, 0);

	struct sockaddr_in sin_in = {
		.sin_family = AF_INET,
	};
	if (!parse_host_port(in_addr, (struct in_addr *)&sin_in.sin_addr.s_addr,
			     &sin_in.sin_port))
		goto err;
	if (!parse_host_port(out_addr,
			     (struct in_addr *)&sin_out.sin_addr.s_addr,
			     &sin_out.sin_port))
		goto err;

	if (!parse_host_port(out_addr2,
			     (struct in_addr *)&sin_out2.sin_addr.s_addr,
			     &sin_out2.sin_port))
		goto err;

	if (bind(in_sock, (struct sockaddr *)&sin_in, sizeof(sin_in))) {
		perror("bind() input");
		exit(EXIT_FAILURE);
	}
	printf("Listening UDP on %s  FWD to %s %s\n", in_addr, out_addr,out_addr2);

	base = event_base_new();

	sig_int = evsignal_new(base, SIGINT, signal_cb, base);
	event_add(sig_int, NULL);
	// it's recommended by libevent authors to ignore SIGPIPE
	signal(SIGPIPE, SIG_IGN);

	in_ev = event_new(base, in_sock, EV_READ | EV_PERSIST, in_read, NULL);
	event_add(in_ev, NULL);

	event_base_dispatch(base);

err:

	if (out_sock>0)
		close(out_sock);

	if (out_sock2>0)
		close(out_sock2);
		
	if (in_sock>0)
		close(in_sock);

	if (in_ev) {
		event_del(in_ev);
		event_free(in_ev);
	}

	if (sig_int)
		event_free(sig_int);

	if (base)
		event_base_free(base);

	

	libevent_global_shutdown();

	return ret;
}

int main(int argc, char **argv)
{
	const struct option long_options[] = {
		{ "out", required_argument, NULL, 'o' },
		{ "out2", required_argument, NULL, 'p' },
		{ "in", required_argument, NULL, 'i' },
		{ "metrics", required_argument, NULL, 'm' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	printf("UDP Forwarder.");
	
	const char *out_addr = defualt_out_addr;
	const char *out_addr2 = defualt_out_addr;
	const char *in_addr = default_in_addr;

	int opt;
	int long_index = 0;
	while ((opt = getopt_long_only(argc, argv, "", long_options,
				       &long_index)) != -1) {
		switch (opt) {		
		case 'o':
			out_addr = optarg;
			break;
		case 'p':
			out_addr2 = optarg;
			break;			

		case 'i':
			in_addr = optarg;
			break;
		case 'm':
			metrics = atoi(optarg);
			if (metrics>2000)
				metrics=2000;
			if(metrics == 0) 
				printf("No parsing, raw UART to UDP only\n");
			start=get_current_time_ms();
			break;
		case 'h':
		default:
			print_usage();
			return EXIT_SUCCESS;
		}
	}

	return handle_data(NULL, 0, out_addr, out_addr2, in_addr);
}
