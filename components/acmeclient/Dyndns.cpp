/*
 * Send periodic updates to a DynDNS provider.
 * Currently supported : noip.com , cloudns.net .
 * Although a global variable is in play, more than one hostname is supported.
 *
 * Copyright (c) 2016, 2017, 2020, 2021 Danny Backx
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
 *
 */
/*
 * NoIP
 *
 * This contains code to send a message to a dynamic dns service such as no-ip.com
 * to register the IP address of this device.
 * The goal is that devices get a fixed DNS name such as esp-device-xxahg.no-ip.com, determined
 * by the owner of the device, and authomatically attach their current IP address (usually variable,
 * depending on the setup of the network they're in) to it.
 *
 * From https://www.noip.com/integrate/request :
 *
 * An example update request string
 *	http://username:password@dynupdate.no-ip.com/nic/update?hostname=mytest.testdomain.com&myip=1.2.3.4
 *
 * An example basic, raw HTTP header GET request
 *	GET /nic/update?hostname=mytest.testdomain.com&myip=1.2.3.4 HTTP/1.0
 *	Host: dynupdate.no-ip.com
 *	Authorization: Basic base64-encoded-auth-string
 *	User-Agent: Bobs Update Client WindowsXP/1.2 bob@somedomain.com
 *
 * The base-encoded-auth-string can be created by using the base64 command and entering
 *	userid:password
 * on a single line. For instance :
 *	acer: {1} base64 
 *	userid:password			<-- substitute your own
 *	dXNlcmlkOnBhc3N3b3JkCg==	<-- this output is what you need
 *	acer: {2} 
 */

/*
 * ClouDNS
 *
 * Just query
 *    https://ipv4.cloudns.net/api/dynamicURL/?q=AUTH
 * where AUTH is a string provided by ClouDNS.net when creating this DNS entry.
 * Return value is "OK" in case of success.
 */

#include <string.h>
#include <esp_log.h>
#include "Dyndns.h"

Dyndns *__dyndns;

// Global variables (shared among Dyndns client instances)
int __dyndns_count = 0;
char *__dyndns_buf = NULL;
// end global variables

Dyndns::Dyndns() {
  memset(&http_config, 0, sizeof(http_config));
  http_config.event_handler = _http_event_handler;

  hostname = ip = auth = buf = 0;
  provider = DD_UNKNOWN;

  __dyndns = this;
  __dyndns_count++;
}

Dyndns::Dyndns(dyndns_provider p) {
  memset(&http_config, 0, sizeof(http_config));
  http_config.event_handler = _http_event_handler;

  hostname = ip = auth = buf = 0;
  provider = p;

  __dyndns = this;
  __dyndns_count++;
}

Dyndns::Dyndns(const char *p) {
  memset(&http_config, 0, sizeof(http_config));
  http_config.event_handler = _http_event_handler;

  hostname = ip = auth = buf = 0;

  __dyndns = this;
  __dyndns_count++;

  if (strcasecmp(p, "cloudns.net") == 0 || strcasecmp(p, "cloudns") == 0)
    provider = DD_CLOUDNS;
  else if (strcasecmp(p, "noip") == 0 || strcasecmp(p, "no-ip") == 0
        || strcasecmp(p, "noip.com") == 0 || strcasecmp(p, "no-ip.com") == 0)
    provider = DD_NOIP;
  else
    provider = DD_UNKNOWN;
}

Dyndns::~Dyndns() {
  __dyndns_count--;
  if (__dyndns_count == 0) {
    free(__dyndns_buf);
    __dyndns_buf = NULL;
  }

}

void Dyndns::setHostname(const char *hostname) {
  this->hostname = (char *)hostname;
}

const char *Dyndns::getHostname() {
  return hostname;
}

void Dyndns::setAddress(const char *ip) {
  this->ip = (char *)ip;
}

const char *Dyndns::getAddress() {
  return ip;
}

void Dyndns::setAuth(const char *auth) {
  this->auth = (char *)auth;
}

const char *Dyndns::getAuth() {
  return auth;
}

/*
 * This method does the work
 *
 * Somewhat more generalized, as the two sites supported differ in setup
 */
bool Dyndns::update() {
  char *query, *header = 0;
  int len;
  bool ok = false;
  // bool https;

  if (provider == DD_NOIP) {
    if (ip != 0)
      len = strlen(get_template2) + strlen(hostname) + strlen(ip) + 5;
    else
      len = strlen(get_template1) + strlen(hostname) + 5;

    header = (char *)malloc(strlen(hdr_template) + strlen(auth) + 5);
    sprintf(header, hdr_template, auth);
  } else if (provider == DD_CLOUDNS) {
    len = strlen(get_template3) + strlen(auth) + 5;
  } else {	// DD_UNKNOWN
    ESP_LOGE(dyndns_tag, "%s(DD_UNKNOWN), aborting", __FUNCTION__);
    return false;
  }

  query = (char *)malloc(len);
  if (query == 0) {
    ESP_LOGE(dyndns_tag, "Could not allocate memory for HTTP query");
    return false;
  }

  if (provider == DD_NOIP) {
    if (ip != 0)
      sprintf(query, get_template2, hostname, ip);
    else
      sprintf(query, get_template1, hostname);
  } else if (provider == DD_CLOUDNS) {
      sprintf(query, get_template3, auth);
  }
  ESP_LOGD(dyndns_tag, "Query %s", query);

  // Secure connection ?
  // https = (strncasecmp(query, "https", 5) == 0);

  // Do it
  http_config.url = query;
  http_client = esp_http_client_init(&http_config);
  if (http_client == 0) {
    ESP_LOGE(dyndns_tag, "Could not initialize");
    free(query);
    return false;
  }
  if (provider == DD_NOIP)
    esp_http_client_set_header(http_client, hdr_header, header);

  if (__dyndns_buf)
    buf = __dyndns_buf;
  else
    __dyndns_buf = buf = (char *)malloc(80);

  // GET
  esp_err_t err = esp_http_client_perform(http_client);
  if (err == ESP_OK) {
    if (esp_http_client_get_status_code(http_client) == 200) {
      ok = true;
      ESP_LOGD(dyndns_tag, "HTTP GET Status = %d, content_length = %d",
        esp_http_client_get_status_code(http_client),
        esp_http_client_get_content_length(http_client));
    } else {
      // Any other code than 200 is probably not good
      ok = false;
      ESP_LOGE(dyndns_tag, "HTTP GET Status = %d, content_length = %d",
        esp_http_client_get_status_code(http_client),
        esp_http_client_get_content_length(http_client));
    }
  } else {
    ESP_LOGE(dyndns_tag, "HTTP GET request failed: %s", esp_err_to_name(err));
  }

  if (provider == DD_CLOUDNS) {
    // Thanks to esp_http_client_perform(), data read is already in this->buf
    ESP_LOGD(dyndns_tag, "received {%s}", buf);

    ok = (strncmp(buf, "OK", 2) == 0);
  }

  free(query);
  if (header)
    free(header);
  if (buf) {
    // free(buf);
    buf = 0;
  }
  esp_http_client_cleanup(http_client);

  return ok;
}

esp_err_t Dyndns::_http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(__dyndns->dyndns_tag, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(__dyndns->dyndns_tag, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(__dyndns->dyndns_tag, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(__dyndns->dyndns_tag, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
	    strncpy(__dyndns_buf, (const char *)evt->data, evt->data_len);
	    __dyndns_buf[evt->data_len] = 0;
            ESP_LOGD(__dyndns->dyndns_tag, "HTTP_EVENT_ON_DATA, len=%d, {%.*s}", evt->data_len,
	      evt->data_len, __dyndns_buf);

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(__dyndns->dyndns_tag, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(__dyndns->dyndns_tag, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}
