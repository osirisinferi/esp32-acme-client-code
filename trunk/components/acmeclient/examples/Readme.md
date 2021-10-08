ACME client library for ESP32 - examples directory

Copyright &copy; 2019, 2020, 2021 by Danny Backx

ACME is a protocol (see RFC8555) for automatic certificate management.
Sites such as letsencrypt.org allow you to obtain free (no charge) certificates in an automated way
using the ACME protocol.

These examples are provided :
- simple.cpp
  A simple ACME client

- dyndns.cpp
  A simple dynamic DNS test application

- standalone.cpp + WebServer.cpp
  An ACME client that runs a web server using the certificate, and also runs dynamic DNS
