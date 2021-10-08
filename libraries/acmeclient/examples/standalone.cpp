/*
 * Full sample client for the ACME library
 *
 *  This assumes that the IoT device is fully reachable over the Internet.
 *
 *  This sample includes DynDNS, ACME, and a builtin web server,
 *  meaning it will periodically refresh its IP address with a service such as no-ip.com,
 *  as well as its certificate, and do the latter with a small builtin web server.
 *
 * Copyright (c) 2019, 2020, 2021 Danny Backx
 *
 * License (MIT license):
 *   Permission is hereby granted, free of charge, to any person obtaining a copy
 *   of this software and associated documentation files (the "Software"), to deal
 *   in the Software without restriction, including without limitation the rights
 *   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *   copies of the Software, and to permit persons to whom the Software is
 *   furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *   THE SOFTWARE.
 */
#define HAVE_LITTLEFS
#undef	HAVE_LISTFILES
#undef	HAVE_REMOVEFILES

#include "StableTime.h"
#include "acmeclient/Acme.h"
#include "acmeclient/Dyndns.h"

#include <esp_wifi.h>
#include <nvs_flash.h>

#include <sntp/sntp.h>

#include "secrets.h"

#include <esp_event.h>
#include <esp_http_server.h>
#include <freertos/task.h>
#include <sys/socket.h>
#include <dirent.h>

#include "webserver.h"

#ifdef HAVE_LITTLEFS
#include <esp_littlefs.h>
#endif

#include "root.pem.h"

static const char *acmeclient_tag = "ACME sample client";
static const char *network_tag = "Network";

static const char *fn_prefix = "/fs";
static const char *acme_fn_prefix = "/fs/acme/standalone";

// Forward
void SetupWifi();
void WaitForWifi();
void StartWebServer(void);
static void RemoveFile(const char *fn);
int ListDir(const char *dn);
void sntp_sync_notify(struct timeval *tvp);

Acme		*acme = 0;
time_t		nowts, boot_time;
bool		wifi_up = false;

// Initial function
void setup(void) {
  esp_err_t err;

  ESP_LOGI(acmeclient_tag, "ACME client (c) 2019, 2020, 2021 by Danny Backx");

  // Make stuff from the underlying libraries quieter
  esp_log_level_set("wifi", ESP_LOG_ERROR);
  esp_log_level_set("system_api", ESP_LOG_ERROR);

  ESP_LOGD(acmeclient_tag, "Starting WiFi "); 
  SetupWifi();

#ifdef HAVE_LITTLEFS
  // Initialize LittleFS
  esp_vfs_littlefs_conf_t lcfg;
  bzero(&lcfg, sizeof(lcfg));

  lcfg.base_path = fn_prefix;
  lcfg.partition_label = "spiffs";
  lcfg.format_if_mount_failed = true;
  err = esp_vfs_littlefs_register(&lcfg);
  if (err != ESP_OK)
    ESP_LOGE(acmeclient_tag, "Failed to register LittleFS %s (%d)", esp_err_to_name(err), err);
  else
    ESP_LOGI(acmeclient_tag, "LittleFS started, mount point %s", fn_prefix);
#else
  // Configure file system access
  esp_vfs_spiffs_conf_t scfg;
  scfg.base_path = fn_prefix;
  scfg.partition_label = NULL;
  scfg.max_files = 10;
  scfg.format_if_mount_failed = false;
  if ((err = esp_vfs_spiffs_register(&scfg)) != ESP_OK)
    ESP_LOGE(acmeclient_tag, "Failed to register SPIFFS %s (%d)", esp_err_to_name(err), err);
  else
    ESP_LOGI(acmeclient_tag, "SPIFFS started, mount point %s", fn_prefix);
#endif

#ifdef HAVE_LISTFILES
  ListDir(fn_prefix);
#endif

#ifdef HAVE_REMOVEFILES
  /*
   * Enabling this code forces the certificate to be renewed, even if it's still very valid.
   */
  RemoveFile("/spiffs/account.json");
  RemoveFile("/spiffs/order.json");
  RemoveFile("/spiffs/certificate.pem");
#endif

  /*
   * Set up the time
   *
   * See https://www.di-mgt.com.au/wclock/help/wclo_tzexplain.html for examples of TZ strings.
   * This one works for Europe : CET-1CEST,M3.5.0/2,M10.5.0/3
   * I assume that this one would work for the US : EST5EDT,M3.2.0/2,M11.1.0
   */
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  stableTime = new StableTime();

  acme = new Acme();
  acme->setFilenamePrefix(acme_fn_prefix);
  acme->setFsPrefix(fn_prefix);
  acme->setUrl(SECRET_URL);
  acme->setEmail(SECRET_EMAIL);

#if defined(SECRET_FTP_SERVER)
  acme->setFtpServer(SECRET_FTP_SERVER);
  acme->setFtpPath(SECRET_FTP_PATH);
  acme->setFtpUser(SECRET_FTP_USER);
  acme->setFtpPassword(SECRET_FTP_PASS);
#endif

  acme->setAccountFilename("account.json");
  acme->setOrderFilename("order.json");
  acme->setAccountKeyFilename("account.pem");
  acme->setCertKeyFilename("certkey.pem");
  acme->setCertificateFilename("certificate.pem");
  acme->setRootCertificate(root_pem_string);

  // No action before time has synced via SNTP
  acme->WaitForTimesync(true);

  /*
   * Watch out before you try this with the production server.
   * Production servers have rate limits, not suitable for debugging.
   */
  // acme->setAcmeServer("https://acme-v02.api.letsencrypt.org/directory");
  // Staging server
  acme->setAcmeServer("https://acme-staging-v02.api.letsencrypt.org/directory");

  // Avoid talking to the server at each reboot
  if (! acme->HaveValidCertificate()) {
    if (acme->getAccountKey() == 0) {
      acme->GenerateAccountKey();
    }
    if (acme->getCertificateKey() == 0) {
      acme->GenerateCertificateKey();
    }
  }

  WaitForWifi();

  StartWebServer();

  if (! acme->HaveValidCertificate()) {
    acme->CreateNewAccount();
    acme->CreateNewOrder();
  } else {
    ESP_LOGI(acmeclient_tag, "Certificate is valid, not obnoxiously querying ACME server because we happen to reboot");
  }

  ESP_LOGD(acmeclient_tag, "... end of setup()");
}

void loop()
{
  struct timeval tv;
  gettimeofday(&tv, 0);
  stableTime->loop(&tv);

  if (! stableTime->timeIsValid())
    return;
  nowts = tv.tv_sec;

  // Record boot time
  if (boot_time == 0) {
    boot_time = nowts;

    char msg[80], ts[24];
    struct tm *tmp = localtime(&boot_time);
    strftime(ts, sizeof(ts), "%Y-%m-%d %T", tmp);
    sprintf(msg, "ACME client boot at %s", ts);
  }

  acme->loop(nowts);
  vTaskDelay(2500 / portTICK_PERIOD_MS); // delay(2500);

  {
    static int nrenews = 0;

    if (nrenews == 1 && boot_time > 35000) {
      nrenews--;
      ESP_LOGI(acmeclient_tag, "Renewing certificate from standalone.cpp");
      acme->RenewCertificate();
    }
  }
}

extern "C" {
  // Minimal ESP32 startup code, if not using Arduino
  void app_main() {
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
      const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
      if (partition != NULL) {
	err = esp_partition_erase_range(partition, 0, partition->size);
	if (!err) {
	  err = nvs_flash_init();
	} else {
	  ESP_LOGE(acmeclient_tag, "Failed to format the broken NVS partition!");
	}
      }
    }
    if (err != ESP_OK) {
      ESP_LOGE(acmeclient_tag, "nvs_flash_init -> %d %s", err, esp_err_to_name(err));
      while (1) ;
    }

    setup();
    while (1)
      loop();
  }
}

// Put your WiFi credentials in "secrets.h", see the sample file.
struct mywifi {
  const char *ssid, *pass, *bssid;
} mywifi[] = {
#ifdef MY_SSID_1
  { MY_SSID_1, MY_WIFI_PASSWORD_1, MY_WIFI_BSSID_1 },
#endif
#ifdef MY_SSID_2
  { MY_SSID_2, MY_WIFI_PASSWORD_2, MY_WIFI_BSSID_2 },
#endif
#ifdef MY_SSID_3
  { MY_SSID_3, MY_WIFI_PASSWORD_3, MY_WIFI_BSSID_3 },
#endif
#ifdef MY_SSID_4
  { MY_SSID_4, MY_WIFI_PASSWORD_4, MY_WIFI_BSSID_4 },
#endif
  { NULL, NULL, NULL}
};

/*
 * esp-idf-4.x style networking event handlers
 */
void event_handler(void *ctx, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  }
}

void discon_event_handler(void *ctx, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  ESP_LOGD(network_tag, "retry to connect to the AP");

  /*
   * We were connected but lost the network. So gracefully shut down open connections,
   * and then try to reconnect to the network.
   */
  ESP_LOGI(network_tag, "STA_DISCONNECTED, restarting");

  if (acme) acme->NetworkDisconnected(ctx, (system_event_t *)event_data);

  esp_wifi_connect();
}

void ip_event_handler(void *ctx, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

  ESP_LOGI(network_tag, "Network connected, ip " IPSTR, IP2STR(&event->ip_info.ip));
  wifi_up = true;

  if (acme) {
    // Note only start running ACME if we're on a network configured for it
    acme->NetworkConnected(ctx, (system_event_t *)event_data);
  }

  sntp_init();
#ifdef	NTP_SERVER_0
  sntp_setservername(0, (char *)NTP_SERVER_0);
#endif
#ifdef	NTP_SERVER_1
  sntp_setservername(1, (char *)NTP_SERVER_1);
#endif
  sntp_set_time_sync_notification_cb(sntp_sync_notify);
}

void SetupWifi(void)
{
  esp_err_t err;
  esp_event_handler_instance_t inst_any_id, inst_got_ip, inst_discon;

  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
      ESP_LOGE(network_tag, "Failed esp_wifi_init, reason %d %s", (int)err, esp_err_to_name(err));
      // FIXME
      return;
  }
  err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (err != ESP_OK) {
      ESP_LOGE(network_tag, "Failed esp_wifi_set_storage, reason %d %s", (int)err, esp_err_to_name(err));
      // FIXME
      return;
  }

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
    event_handler, NULL, &inst_any_id);
  esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, 
    discon_event_handler, NULL, &inst_discon);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
    ip_event_handler, NULL, &inst_got_ip);
}

void WaitForWifi(void)
{
  ESP_LOGD(network_tag, "Waiting for wifi");
 
  wifi_config_t wifi_config;
  for (int ix = 0; mywifi[ix].ssid != 0; ix++) {
    memset(&wifi_config, 0, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, mywifi[ix].ssid);
    strcpy((char *)wifi_config.sta.password, mywifi[ix].pass);
    if (mywifi[ix].bssid) {
      int r = sscanf(mywifi[ix].bssid, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
        &wifi_config.sta.bssid[0],
        &wifi_config.sta.bssid[1],
        &wifi_config.sta.bssid[2],
        &wifi_config.sta.bssid[3],
        &wifi_config.sta.bssid[4],
        &wifi_config.sta.bssid[5]);
      wifi_config.sta.bssid_set = true;
      if (r != 6) {
	ESP_LOGE(network_tag, "Could not convert MAC %s into acceptable format", mywifi[ix].bssid);
	memset(wifi_config.sta.bssid, 0, sizeof(wifi_config.sta.bssid));
	wifi_config.sta.bssid_set = false;
      }
    } else
      memset(wifi_config.sta.bssid, 0, sizeof(wifi_config.sta.bssid));

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
      ESP_LOGE(network_tag, "Failed to set wifi mode to STA");
      // FIXME
      return;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
      ESP_LOGE(network_tag, "Failed to set wifi config");
      // FIXME
      return;
    }
    ESP_LOGI(network_tag, "Try wifi ssid [%s]", wifi_config.sta.ssid);
    err = esp_wifi_start();
    if (err != ESP_OK) {
      ESP_LOGE(network_tag, "Failed to start wifi");
      // FIXME
      return;
    }

    for (int cnt = 0; cnt < 40; cnt++) {
      vTaskDelay(100 / portTICK_PERIOD_MS); // delay(100);
      if (wifi_up) {
        ESP_LOGI(network_tag, ".. connected to wifi (attempt %d)", cnt+1);
        return;
      }
    }
  }
}

void NoIP() {
  ESP_LOGI(acmeclient_tag, "Registering with no-ip.com ... ");
  Dyndns *d = new Dyndns();
  d->setHostname(NOIP_HOSTNAME);
  d->setAuth(NOIP_AUTH);
  if (d->update())
    ESP_LOGI(acmeclient_tag, "succeeded");
  else
    ESP_LOGE(acmeclient_tag, "failed");
}

static void RemoveFile(const char *fn) {
  if (unlink(fn) < 0)
    ESP_LOGE(acmeclient_tag, "Could not unlink %s", fn);
  else
    ESP_LOGI(acmeclient_tag, "Removed %s", fn);
}

/*
 * List all files on LittleFS
 */
int ListDir(const char *dn) {
  DIR *d = opendir(dn);
  struct dirent *de;
  int count = 0;

  if (d == 0) {
    ESP_LOGD("fs", "%s: dir null", __FUNCTION__);
    return 0;
  }

  rewinddir(d);
  while (1) {
    de = readdir(d);
    if (de == 0) {
      ESP_LOGD("fs", "Dir %s contained %d entries", dn, count);
      closedir(d);
      return count;
    }
    count++;
    ESP_LOGI("fs", "Dir %s entry %d : %s", dn, count, de->d_name);

    // Recursively descend
    if (de->d_type & DT_DIR) {
      // This doesn't happen with current implementation, but let's be sure
      // Don't descend into current directory
      if (strcmp(de->d_name, ".") == 0)
        continue;

      int len = strlen(dn) + strlen(de->d_name) + 2;
      char *n = (char *)malloc(len);		// Note freed locally
      strcpy(n, dn);
      strcat(n, "/");
      strcat(n, de->d_name);
      count += ListDir(n);
      free(n);
    }
  }
}

void sntp_sync_notify(struct timeval *tvp) {
  ESP_LOGE(acmeclient_tag, "%s", __FUNCTION__);
  if (acme)
    acme->TimeSync(tvp);
}
