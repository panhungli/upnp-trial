#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>
#include <timers.h>
#include <queue.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <lwip/ip_addr.h>
#include <lwip/api.h>
#include <lwip/netbuf.h>
#include <lwip/igmp.h>
#include <wlan/wlan_test_inc.h>
#include <wifi/wifi_conf.h>
#include <wifi/wifi_util.h>
#include <lwip_netconf.h>

#include "upnp_upnp.h"
#include "upnp_httpd.h"

#define RUN_IN_AP (1)
#if RUN_IN_AP
extern struct netif xnetif[NET_IF_NUM]; 
#endif
/** User friendly FreeRTOS delay macro */
#define delay_ms(ms) vTaskDelay(ms / portTICK_PERIOD_MS)

const unsigned int my_wifi_events[] = {
  WIFI_EVENT_CONNECT,
  WIFI_EVENT_DISCONNECT
};
static unsigned char _wifi_evt_inited = 0;
/** Semaphore to signal wifi availability */
static SemaphoreHandle_t wifi_alive;

/**
  * @brief This is the multicast task
  * @param arg user supplied argument from xTaskCreate
  * @retval None
  */
static void mcast_task(void *arg)
{
  //vTaskDelay(10000);
    //xSemaphoreTake(wifi_alive, portMAX_DELAY);
    //xSemaphoreGive(wifi_alive);
  
    (void)upnp_server_init();
    while( upnp_alive()==1 ) {
        delay_ms(2000);
    }
    vTaskDelete(NULL);
}

void my_wifi_evt_handler(char *buf, int buf_len, int flags, void* handler_user_data )
{
  unsigned int event_code = (unsigned int)handler_user_data;
  switch( event_code ) {
  case WIFI_EVENT_DISCONNECT:
    printf("WIFI_EVENT_DISCONNECT\n");
    upnp_server_end();
    break;
  case WIFI_EVENT_CONNECT:
    printf("WIFI_EVENT_CONNECT\n");
    // For WPA/WPA2 mode, indication of connection does not mean data can be
    // 		correctly transmitted or received. Data can be correctly transmitted or
    // 		received only when 4-way handshake is done.
    // Please check WIFI_EVENT_FOURWAY_HANDSHAKE_DONE event
    // Sample: return mac address
    if(buf != NULL && buf_len == 6)
    {
      printf("Connect indication received: %02x:%02x:%02x:%02x:%02x:%02x",
	     buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
    }
    if( xTaskCreate(&mcast_task, "mcast_task", 1024, NULL, tskIDLE_PRIORITY + 3, NULL) != pdPASS ) { printf("mcast failed"); return; }
    return;
  }
}

void my_wifi_evt_init(void)
{
  int i;
  int event_count = (sizeof(my_wifi_events)/sizeof(unsigned int));
  if( _wifi_evt_inited == 1 ) return;
  _wifi_evt_inited = 1;
  for( i = 0; i < event_count; i++ ) {
    wifi_reg_event_handler(my_wifi_events[i], (rtw_event_handler_t)my_wifi_evt_handler, (void *)my_wifi_events[i]);
  }
}

/**
  * @brief This is the wifi connection task
  * @param arg user supplied argument from xTaskCreate
  * @retval None
  */
static void wifi_task(void *pvParameters)
{
  vTaskDelay(5000);
  my_wifi_evt_init();
#if RUN_IN_AP
  const char *ssid = "AMEBA1";
  const rtw_security_t security_type = RTW_SECURITY_OPEN;
  const char *password = "00000000";
  const int channel = 3;

  xSemaphoreTake(wifi_alive, portMAX_DELAY);
  printf("\n\r Disable Wi-Fi\n");
  wifi_off();
  vTaskDelay(20);

  printf("\n\r Enable Wi-Fi with AP mode\n");
  if(wifi_on(RTW_MODE_AP) < 0){
    printf("\n\r ERROR: wifi_on failed\n");
    return;
  }

  printf("WiFi: connecting to WiFi\n");
  if(wifi_start_ap(ssid, security_type, password, strlen(ssid), strlen(password), channel) < 0) {
    printf("ERROR: wifi_start_ap failed\n");
    return;
  }
  dhcps_init(&xnetif[0]);
  printf("\n\r Check AP running\n");
  int timeout = 20;
  while(1) {
    char essid[33];
    if(wext_get_ssid(WLAN0_NAME, (unsigned char *) essid) > 0) {
      if(strcmp((const char *) essid, (const char *)ssid) == 0) {
	printf("\n\r %s started\n", ssid);
	xSemaphoreGive(wifi_alive);
	break;
      }
    }
    if(timeout == 0) {
      printf("\n\r ERROR: Start AP timeout\n");
      return;
    }
    vTaskDelay(1 * configTICK_RATE_HZ);
    timeout --;
  }
#else
  const char *ssid = "LAB-RT210";
  const rtw_security_t security_type = RTW_SECURITY_WPA2_MIXED_PSK;
  const char *password = "0222235121";
  int ret, retry = 0;
  //skip, because module is started with standalone

  xSemaphoreTake(wifi_alive, portMAX_DELAY);
  /*
  printf("\n\r Disable Wi-Fi\n");
  wifi_off();
  vTaskDelay(20);
  */
  printf("\n\r Enable Wi-Fi with STA mode\n");
  if(wifi_on(RTW_MODE_STA) < 0){
    printf("\n\r ERROR: wifi_on failed\n");
    return;
  }
  while( retry < 3 ) {
    if( ( ret = wifi_connect(ssid, security_type, password, strlen(ssid), strlen(password), -1, NULL) )
	== RTW_SUCCESS) {
      uint8_t ret_dhcp = LwIP_DHCP(0, DHCP_START);
      // IP assigned by DHCP
      if( ret_dhcp == DHCP_ADDRESS_ASSIGNED ) {
	xSemaphoreGive(wifi_alive);
      }
      break;
    }
    printf("wifi connect failed: ret=%d, retry=%d", ret, (retry+1) );
    retry += 1;
  }
#endif
  vTaskDelete(NULL);
}

void upnp_demo_init(void)
{
    vSemaphoreCreateBinary(wifi_alive);
    if( xTaskCreate(&wifi_task, "wifi_task",  1024, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS ) { printf("wifi failed"); return; } 
    if( xTaskCreate(&httpd_task, "http_server", 1024, NULL, tskIDLE_PRIORITY + 3, NULL) != pdPASS ) { printf("httpd failed"); return; }
    //if( xTaskCreate(&mcast_task, "mcast_task", 1024, NULL, tskIDLE_PRIORITY + 3, NULL) != pdPASS ) { printf("mcast failed"); return; }
}
