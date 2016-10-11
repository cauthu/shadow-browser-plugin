this experiment tests the fix for issue #10.

apply the fake_error.patch (against revision
20efc5983e3084a3a694db8526cbfc4ea0cb3339) to make the webserver
alternate faking error on the main resource

the driver output should be something like this:

```
00:16:51 INFO  - driver_renderer.cpp :162, _report_result() ::   loadnum= 1, webmode= vanilla, proxyMode= tproxy: loadResult= FAILED: start= 1002091 plt= 0 page= [cnn] ttfb= 0 numReqs= 1 numSuccess= 0 numFailed= 0 numPostDOMLoadEvent= 0
00:17:29 INFO  - driver_renderer.cpp :162, _report_result() ::   loadnum= 2, webmode= vanilla, proxyMode= tproxy: loadResult= OK: start= 1036888 plt= 9547 page= [cnn] ttfb= 421 numReqs= 1 numSuccess= 1 numFailed= 0 numPostDOMLoadEvent= 0
00:18:00 INFO  - driver_renderer.cpp :162, _report_result() ::   loadnum= 3, webmode= vanilla, proxyMode= tproxy: loadResult= FAILED: start= 1071238 plt= 0 page= [cnn] ttfb= 0 numReqs= 1 numSuccess= 0 numFailed= 0 numPostDOMLoadEvent= 0
00:18:51 INFO  - driver_renderer.cpp :162, _report_result() ::   loadnum= 4, webmode= vanilla, proxyMode= tproxy: loadResult= OK: start= 1119322 plt= 9546 page= [cnn] ttfb= 421 numReqs= 1 numSuccess= 1 numFailed= 0 numPostDOMLoadEvent= 0
```
