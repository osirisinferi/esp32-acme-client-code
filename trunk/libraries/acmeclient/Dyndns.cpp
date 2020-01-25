/*
 * Copyright (c) 2016, 2017, 2020 Danny Backx
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

#include <Arduino.h>
#include "Dyndns.h"

Dyndns::Dyndns() {
  url = (char *)"dynupdate.no-ip.com";		// Default, can be overruled by method

  memset(&http_config, 0, sizeof(http_config));
  http_config.event_handler = _http_event_handler;

  hostname = ip = auth = 0;
}

Dyndns::~Dyndns() {
}

void Dyndns::setHostname(const char *hostname) {
  this->hostname = (char *)hostname;
}

void Dyndns::setAddress(const char *ip) {
  this->ip = (char *)ip;
}

void Dyndns::setAuth(const char *auth) {
  this->auth = (char *)auth;
}

void Dyndns::setUrl(const char *url) {
  this->url = (char *)url;
}

boolean Dyndns::update() {
  char *query, *header;
  int len;
  boolean ok = false;

  if (ip != 0)
    len = strlen(get_template2) + strlen(url) + strlen(hostname) + strlen(ip) + 5;
  else
    len = strlen(get_template1) + strlen(url) + strlen(hostname) + 5;

  query = (char *)malloc(len);
  if (query == 0) {
    ESP_LOGE(dyndns_tag, "Could not allocate memory for HTTP query");
    return false;
  }

  if (ip != 0)
    sprintf(query, get_template2, url, hostname, ip);
  else
    sprintf(query, get_template1, url, hostname);
  ESP_LOGI(dyndns_tag, "Query %s", query);

  //
  header = (char *)malloc(strlen(hdr_template) + strlen(auth) + 5);
  sprintf(header, hdr_template, auth);

  // Do it
  http_config.url = query;
  http_client = esp_http_client_init(&http_config);
  if (http_client == 0) {
    ESP_LOGE(dyndns_tag, "Could not initialize");
    free(query);
    return false;
  }
  esp_http_client_set_header(http_client, hdr_header, header);

  // GET
  esp_err_t err = esp_http_client_perform(http_client);
  if (err == ESP_OK) {
    ESP_LOGD(dyndns_tag, "HTTP GET Status = %d, content_length = %d",
      esp_http_client_get_status_code(http_client),
      esp_http_client_get_content_length(http_client));
    ok = true;
  } else {
    ESP_LOGE(dyndns_tag, "HTTP GET request failed: %s", esp_err_to_name(err));
  }

  free(query); free(header);
  esp_http_client_cleanup(http_client);

  return ok;

#if 0
  if (err == 0) {
    ESP_LOGI(dyndns_tag, "Success ");
  } else if (err == 0) {
    // timeout
    ESP_LOGE(dyndns_tag, "Timeout");
  } else {
    ESP_LOGE(dyndns_tag, "HTTP GET failed");
  }
#endif
}

static const char *sdyndns_tag = "static dyndns";

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(sdyndns_tag, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(sdyndns_tag, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(sdyndns_tag, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(sdyndns_tag, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(sdyndns_tag, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                // printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(sdyndns_tag, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(sdyndns_tag, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}