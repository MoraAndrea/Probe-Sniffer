//main source file for the MalnatiProject firmware of ESP32

#include <stdio.h> //DEBUG
#include <ctype.h> //DEBUG
#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "sys/socket.h"

/*Set the SSID and Password via "make menuconfig"*/
#define DEFAULT_SSID CONFIG_WIFI_SSID
#define DEFAULT_PWD CONFIG_WIFI_PASSWORD

#define DEFAULT_SCAN_METHOD WIFI_FAST_SCAN
#define DEFAULT_RSSI -127
#define DEFAULT_AUTHMODE WIFI_AUTH_OPEN
#define DEFAULT_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL

#define BUFLEN 512
#define PORT 45445
#define SPORT 48448
#define LISTENQ 3
#define IPLEN 17

//ESP32 status codes
#define ST_DISCONNECTED 0
#define ST_CONNECTED 1
#define ST_GOT_IP 3
#define ST_ERR 4
#define ST_SNIFFING 5
#define ST_WAITING_TIME 6

#define HEADER_LEN 10
#define MAC_LEN 17
#define TIME_LEN 22
#define SSID_LEN 34

//codes received from server
#define CODE_OK 200
#define CODE_TIME 202
#define CODE_RESET 203
#define CODE_READY 100
#define CODE_DATA 101

struct packet_info
{
 char mac[MAC_LEN];
 char ssid[SSID_LEN];
 char timestamp[TIME_LEN];
 //char hash[HASH_LEN];
 int strength;
};

struct packet_node
{
 struct packet_info packet;
 struct packet_node *next;
};

struct status
{
 int status_value;
 char server_ip[IPLEN];
 int port;
 char srv_time[TIME_LEN];
 time_t client_time;
 struct packet_node *packet_list;
 int total_length;
 esp_timer_handle_t timer;
};

struct status st;

void clear_data()
{
 struct packet_node *next;
 struct packet_node *p=st.packet_list;
 while(p!=NULL)
 {
  next=p->next;
  free(p);
  p=next;
 }
 st.total_length=0;
}

void start_timer()
{
 esp_err_t ret;
 uint64_t usec=5000000;
 ret = esp_timer_start_once(st.timer, usec);
 ESP_ERROR_CHECK( ret );
}

//handle the end of the timer: send data to server then reset timer and return sniffing
void timer_handle()
{
 printf("'bling'\n"); //DEBUG
 //send_data();
 start_timer();
}

//sniffs packets then sends those to server in infinite loop
void sniffer()
{
 clear_data();
 start_timer();
 //start_sniffing();
}

//save time received from server and time on client when it arrives
void save_timestamp(char *buf)
{
 strncpy(st.srv_time, buf+2, TIME_LEN-2);
 st.srv_time[TIME_LEN-2]='\0';
 st.client_time=time(NULL);
}

//returns code received in header from server
int read_header(char *buf)
{
 char head_val;
 sscanf(buf, "%c", &head_val);
 return (int)head_val;
}

//receive packets frome server and decides what to do
void recv_from_server(int s)
{
 char buf[BUFLEN];
 int recv_len, code;

 recv_len=1;
 while(recv_len>0)
 {
  recv_len=recv(s, buf, BUFLEN, 0);
  code=read_header(buf);
  switch(code)
  {
   case CODE_OK:
    st.status_value=ST_WAITING_TIME;
    break;
   case CODE_TIME:
    st.status_value=ST_SNIFFING;
    save_timestamp(buf);
    //start_timer();
    //start_sniffing();
    break;
   case CODE_RESET:
    close(s);
    esp_restart();
    break;
   default:
    close(s);
    esp_restart();
    break;
  }
 }
 close(s);
}

//first connection with server at startup (TCP:48448): sends OK + MAC address
void send_ready()
{
 int s, result, i, sent_len;
 struct sockaddr_in str_sock_s;
 char buf[BUFLEN];
 //unsigned int sockaddrlen;
 struct in_addr in_addr;
 uint8_t mac[6];

 s=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

 if(s<0)
 {
  st.status_value=ST_ERR;
  return;
 }

 memset(&str_sock_s, 0, sizeof(str_sock_s));
 str_sock_s.sin_family=AF_INET;
 str_sock_s.sin_port=htons(SPORT);
 if(inet_aton(st.server_ip, &in_addr)==0)
 {
  close(s);
  st.status_value=ST_ERR;
  return;
 }
 str_sock_s.sin_addr=in_addr;

 //printf("Trying to connect to %s:%d\n", inet_ntoa(str_sock_s.sin_addr), ntohs(str_sock_s.sin_port));

 result=connect(s, (struct sockaddr *)&str_sock_s, sizeof(str_sock_s));
 if(result==-1)
 {
  close(s);
  st.status_value=ST_ERR;
  return;
 }

 if(esp_wifi_get_mac(ESP_IF_WIFI_STA, mac) != ESP_OK)
 {
  close(s);
  st.status_value=ST_ERR;
  return;
 }

 char c=CODE_READY;
 sprintf(buf, "%c", c);
 for(i=0; i<6; i++)
  sprintf(buf+HEADER_LEN+(i*3), "%02x:", mac[i]);
 buf[9+(i*3)]='\0';

 sent_len=send(s, buf, HEADER_LEN+MAC_LEN, 0);
 if(sent_len<0)
 {
  close(s);
  st.status_value=ST_ERR;
  return;
 }
 recv_from_server(s);
}

//receive the broadcast packet from server (UDP:45445) containing server ip address
void acquire_server_ip()
{
 int s, result, recv_len, i;
 struct sockaddr_in str_sock_s, str_sock_c;
 char buf[BUFLEN];
 unsigned int sockaddrlen;

 s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

 if(s<0)
 {
  st.status_value=ST_ERR;
  return;
 }

 memset(&str_sock_s, 0, sizeof(str_sock_s));
 str_sock_s.sin_family=AF_INET;
 str_sock_s.sin_port=htons(PORT);
 str_sock_s.sin_addr.s_addr=htonl(INADDR_ANY);

 result=bind(s, (struct sockaddr *)&str_sock_s, sizeof(str_sock_s));
 if(result==-1)
 {
  close(s);
  return;
 }

 sockaddrlen=sizeof(struct sockaddr_in);

 while(1)
 {
  recv_len=recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *)&str_sock_c, &sockaddrlen);
  if(strncmp(buf, "server:", 7)==0)
  {
   for(i=0; i<recv_len-7 && buf[7+i]!=':'; i++)
   st.server_ip[i]=buf[7+i];
   st.server_ip[i]='\0';
   st.status_value=ST_GOT_IP;
   close(s);
   break;
  }
 }
}

//handle wifi connection related events
static esp_err_t event_handler_wifi(void *ctx, system_event_t *event)
{
 switch (event->event_id) {
  case SYSTEM_EVENT_STA_START:
   //ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
   ESP_ERROR_CHECK(esp_wifi_connect());
   break;
  case SYSTEM_EVENT_STA_GOT_IP:
   //ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
   //ESP_LOGI(TAG, "Got IP: %s\n",ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
   st.status_value=ST_CONNECTED;
   break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
   //ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
   ESP_ERROR_CHECK(esp_wifi_connect());
   st.status_value=ST_DISCONNECTED;
   break;
  default:
   break;
 }
 return ESP_OK;
}

//does what the name says
static void setup_and_connect_wifi(void)
{
 tcpip_adapter_init();
 ESP_ERROR_CHECK(esp_event_loop_init(event_handler_wifi, NULL));

 wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
 ESP_ERROR_CHECK(esp_wifi_init(&cfg));
 wifi_config_t wifi_config = {
  .sta = {
   .ssid = DEFAULT_SSID,
   .password = DEFAULT_PWD,
   //.scan_method = DEFAULT_SCAN_METHOD,
   //.sort_method = DEFAULT_SORT_METHOD,
   //.threshold.rssi = DEFAULT_RSSI,
   //.threshold.authmode = DEFAULT_AUTHMODE,
  },
 };
 ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
 ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
 ESP_ERROR_CHECK(esp_wifi_start());
}

/* #HEX+ASCII PRINTER#
//print payload data in hex and ascii, for debug purposes
void print_data(unsigned char *buf, int len)
{
 int i, j, c, fill_spaces;
 for(i=0; i < len; i++)
 {
  printf("%02x ", buf[i] & 0xff);
  if(((i+1)%16)==0 && i!=0)
  {
   printf("  |  ");
   for(j=15; j>=0; j--)
   {
    c=buf[i-j];
    if(isprint(c))
     printf("%c", (char)c);
    else
     printf(".");
   }
   printf("\n");
  }
  else if(((i+1)%8)==0 && i!=0)
   printf("   ");
 }
 fill_spaces=16-(len%16);
 if(fill_spaces>=8)
  fill_spaces++;
 for(i=0; i<fill_spaces; i++)
  printf("   ");

 printf("  |  ");
 for(j=(len%16); j>=0; j--)
 {
  c=buf[len-j];
  if(isprint(c))
   printf("%c", (char)c);
  else
   printf(".");
 }
 printf("\n\n");
}

void print_packet_info_mgmt(wifi_promiscuous_pkt_t *pkt)
{
 printf("Strength: %d\nChannel: %d\nPayload:\n", pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
 print_data(pkt->payload, pkt->rx_ctrl.sig_len);
}
*/

//handle the packet sniffed in promiscuous mode (we are interested in management packets)
void event_handler_promiscuous(void *buf, wifi_promiscuous_pkt_type_t type)
{
 printf("Packet received\n"); //DEBUG
 switch(type)
 {
  case WIFI_PKT_MGMT:
   printf("MANAGEMENT\n"); //DEBUG
   //print_packet_info_mgmt((wifi_promiscuous_pkt_t *)buf);
   break;
  case WIFI_PKT_DATA:
   printf("DATA\n");
   //print_data(((wifi_promiscuous_pkt_t *)buf)->payload, ((wifi_promiscuous_pkt_t *)buf)->rx_ctrl.sig_len);
   //print_packet_info((wifi_promiscuous_pkt_t *)buf);
   break;
  default:
   printf("OTHER PACKET\n"); //DEBUG
   break;
 }
}

//omen nomen
int setup_and_listen_promiscuous()
{
 wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

 ESP_ERROR_CHECK(esp_wifi_init(&cfg));
 ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(event_handler_promiscuous));
 ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
 return 1;
}

//initialize the main structure
void initialize_st()
{
 st.status_value=ST_DISCONNECTED;
 strcpy(st.server_ip, "\0");
 st.port=-1;
 strcpy(st.srv_time, "\0");
 st.client_time=0;
 st.packet_list=NULL;
 st.total_length=0;
 st.timer=NULL;
}

//main function
void app_main()
{
 esp_err_t ret = nvs_flash_init();
 if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
  ESP_ERROR_CHECK(nvs_flash_erase());
  ret = nvs_flash_init();
 }
 ESP_ERROR_CHECK( ret );

 initialize_st();

 //create timer
 esp_timer_create_args_t create_args;
 create_args.callback = timer_handle;
 create_args.arg = NULL;
 create_args.dispatch_method = ESP_TIMER_TASK;
 create_args.name = "timer\0";
 ret = esp_timer_create(&create_args, &(st.timer));
 ESP_ERROR_CHECK( ret );

 //setup_and_listen_promiscuous();
/* setup_and_connect_wifi();
 while(st.status_value==ST_DISCONNECTED);

 acquire_server_ip();
 if(st.status_value==ST_GOT_IP)
 {
  printf("IP Acquired: %s\n", st.server_ip);
 }
 else
  esp_restart();
 send_ready();
 if(st.status_value!=ST_SNIFFING)
  esp_restart();
 //sniffer();
 esp_restart();
*/

 start_timer();
}

