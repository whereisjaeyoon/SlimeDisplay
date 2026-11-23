```sh
Patron@LOAN-MGX0D3FHHJ ~ % dns-sd -B _slimedisplay._tcp local.
Browsing for _slimedisplay._tcp.local.
DATE: ---Mon 23 Nov 2026---
 2:37:16.721  ...STARTING...
Timestamp     A/R    Flags  if Domain               Service Type         Instance Name
 2:37:16.724  Add        3   1 local.               _slimedisplay._tcp.  SlimeDisplay-Test
 2:37:16.724  Add        2  11 local.               _slimedisplay._tcp.  SlimeDisplay-Test
 2:37:42.475  Rmv        1   1 local.               _slimedisplay._tcp.  SlimeDisplay-Test
 2:37:42.475  Rmv        0  11 local.               _slimedisplay._tcp.  SlimeDisplay-Test
```

```sh
Patron@LOAN-MGX0D3FHHJ ~ % dns-sd -R "SlimeDisplay-Test" _slimedisplay._tcp local. 8080 
Registering Service SlimeDisplay-Test._slimedisplay._tcp.local. port 8080
DATE: ---Mon 23 Nov 2026---
 2:38:06.121  ...STARTING...
 2:38:06.832  Got a reply for service SlimeDisplay-Test._slimedisplay._tcp.local.: Name now registered and active
```