ACME client library for ESP32

Copyright &copy; 2019, 2020, 2021 by Danny Backx

ACME is a protocol (see <a href="https://tools.ietf.org/html/rfc8555">RFC8555</a>) for automatic certificate management.
Sites such as letsencrypt.org allow you to obtain free (no charge) certificates in an automated way
using the ACME protocol.

This library allows you to get certificates for IoT devices based on the ESP32.

More info in <a href="https://sourceforge.net/p/esp32-acme-client/code/HEAD/tree/trunk/Readme.md">components/acmeclient/Readme.md</a> .

Specific configuration examples and technology overview is in <a href="https://sourceforge.net/p/esp32-acme-client/code/HEAD/tree/trunk/components/acmeclient/Configurations.md">Configurations.md</a>.

Important note : some versions of the esp-idf esp_http_client_fetch_headers have a bug which causes the nonce in ACME communication to get cut off. Fix by either setting buffer size to e.g. 800 or getting fixed code. If you have nonce mismatches, my best bet is you are building with a version of esp-idf with that bug.
