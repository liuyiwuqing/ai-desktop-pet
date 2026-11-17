#include "svc_net/netif_ip.h"
#include "pet/netcfg.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "netif_ip";

esp_err_t pet_netif_apply_ip(esp_netif_t *netif,const char *mode,const char *ip,
    const char *netmask,const char *gateway,const char *dns,const char *hostname)
{
    if(!netif) return ESP_ERR_INVALID_ARG;
    if(hostname&&hostname[0]) {
        if(!pet_netcfg_hostname(hostname)) return ESP_ERR_INVALID_ARG;
        esp_err_t err=esp_netif_set_hostname(netif,hostname);
        if(err!=ESP_OK) return err;
    }
    uint8_t quad[4];
    if(pet_netcfg_static(mode)) {
        if(!ip||!netmask||!gateway) return ESP_ERR_INVALID_ARG;
        if(!pet_netcfg_ipv4(ip,quad)||!pet_netcfg_netmask(netmask)||!pet_netcfg_ipv4(gateway,quad)||
            !pet_netcfg_same_subnet(ip,netmask,gateway)) return ESP_ERR_INVALID_ARG;
        if(dns&&dns[0]&&!pet_netcfg_ipv4(dns,quad)) return ESP_ERR_INVALID_ARG;
        esp_err_t err=esp_netif_dhcpc_stop(netif);
        if(err!=ESP_OK&&err!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return err;
        esp_netif_ip_info_t info={0};
        if(esp_netif_str_to_ip4(ip,&info.ip)!=ESP_OK||
            esp_netif_str_to_ip4(netmask,&info.netmask)!=ESP_OK||
            esp_netif_str_to_ip4(gateway,&info.gw)!=ESP_OK) return ESP_ERR_INVALID_ARG;
        err=esp_netif_set_ip_info(netif,&info);
        if(err!=ESP_OK) return err;
        if(dns&&dns[0]) {
            esp_netif_dns_info_t dns_info={0};
            dns_info.ip.type=ESP_IPADDR_TYPE_V4;
            if(esp_netif_str_to_ip4(dns,&dns_info.ip.u_addr.ip4)!=ESP_OK) return ESP_ERR_INVALID_ARG;
            err=esp_netif_set_dns_info(netif,ESP_NETIF_DNS_MAIN,&dns_info);
            if(err!=ESP_OK) return err;
        }
        ESP_LOGI(TAG,"static address applied (gw=%s dns=%s)",gateway,dns&&dns[0]?dns:"dhcp");
        return ESP_OK;
    }
    if(!pet_netcfg_mode(mode)) return ESP_ERR_INVALID_ARG;
    esp_err_t err=esp_netif_dhcpc_start(netif);
    return err==ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED?ESP_OK:err;
}
