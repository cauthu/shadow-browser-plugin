the page_model is from the chrome repository at
chrome/38.0.2125.122/model_extractor/samples/alibaba/alibaba-page_model-10152016.json
at revision bbef4068fc212d6c0ac9c9e4078acd079ca5bdf5

before should not see loading of resources 9 and after

```
00:16:42.091 INFO  - driver_renderer.cpp :354, _renderer_load_page() ::   driver= 1: start loading page [testpage]
00:16:42.091 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 1:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 2:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 3:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 4:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 5:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 6:0
00:16:43.013 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 7:0
00:16:43.262 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 8:0
00:16:44.383 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 1:0 finished successfully; new num_succes_reqs_: 1
00:16:46.263 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 3:0 finished successfully; new num_succes_reqs_: 2
00:16:46.263 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 4:0 finished successfully; new num_succes_reqs_: 3
00:16:46.309 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 8:0 finished successfully; new num_succes_reqs_: 4
00:16:46.355 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 7:0 finished successfully; new num_succes_reqs_: 5
00:16:46.362 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 2:0 finished successfully; new num_succes_reqs_: 6
00:16:46.412 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 6:0 finished successfully; new num_succes_reqs_: 7
00:16:46.412 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 5:0 finished successfully; new num_succes_reqs_: 8
00:16:46.417 INFO  - driver_renderer.cpp :193, _renderer_handle_PageLoaded() ::   driver= 1: DOM "load" event has fired; start grace period
00:16:49.417 INFO  - driver.cpp :177, _on_grace_period_timer_fired() ::   driver= 1: done grace period
00:16:49.417 INFO  - driver_renderer.cpp :162, _report_result() ::   loadnum= 1, webmode= vanilla, proxyMode= tproxy: loadResult= OK: startSec= 1002 plt= 4326 page= [testpage] ttfb= 421 numReqs= 8 numSuccess= 8 numFailed= 0 numAfterDOMLoadEvent= 0
00:16:49.508 INFO  - driver_tproxy.cpp :108, _tproxy_on_establish_tunnel_resp() ::   driver= 1: CSP allRecvByteCountSoFar: 570750 usefulRecvByteCountSoFar: 553642
00:16:49.508 INFO  - driver_tproxy.cpp :159, _tproxy_on_set_auto_start_defense_on_next_send_resp() ::   driver= 1: tproxy is ready
00:16:49.508 INFO  - driver_renderer.cpp :317, _start_thinking() ::   driver= 1: start thinking for 25159.5 ms
```

with fix, should see resources 9-20 loaded:
```
00:16:42.091 VER-1 - driver_renderer.cpp :352, _renderer_load_page() ::   driver= 1: picked new page_model_idx_= 0
00:16:42.091 INFO  - driver_renderer.cpp :354, _renderer_load_page() ::   driver= 1: start loading page [testpage]
00:16:42.091 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 1:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 2:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 3:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 4:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 5:0
00:16:42.637 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 6:0
00:16:43.013 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 7:0
00:16:43.262 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 8:0
00:16:44.383 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 1:0 finished successfully; new num_succes_reqs_: 1
00:16:46.263 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 3:0 finished successfully; new num_succes_reqs_: 2
00:16:46.263 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 4:0 finished successfully; new num_succes_reqs_: 3
00:16:46.309 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 8:0 finished successfully; new num_succes_reqs_: 4
00:16:46.355 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 7:0 finished successfully; new num_succes_reqs_: 5
00:16:46.362 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 2:0 finished successfully; new num_succes_reqs_: 6
00:16:46.412 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 6:0 finished successfully; new num_succes_reqs_: 7
00:16:46.412 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 5:0 finished successfully; new num_succes_reqs_: 8
00:16:46.417 INFO  - driver_renderer.cpp :193, _renderer_handle_PageLoaded() ::   driver= 1: DOM "load" event has fired; start grace period
00:16:46.417 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 12:0
00:16:46.417 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 13:0
00:16:46.417 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 14:0
00:16:46.417 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 15:0
00:16:46.417 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 16:0
00:16:46.417 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 17:0
00:16:46.417 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 18:0
00:16:46.417 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 19:0
00:16:46.417 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 20:0
00:16:48.388 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 12:0 finished successfully; new num_succes_reqs_: 9
00:16:48.388 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 13:0 finished successfully; new num_succes_reqs_: 10
00:16:48.388 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 14:0 finished successfully; new num_succes_reqs_: 11
00:16:48.558 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 17:0 finished successfully; new num_succes_reqs_: 12
00:16:48.808 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 16:0 finished successfully; new num_succes_reqs_: 13
00:16:48.808 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 16:1
00:16:48.933 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 20:0 finished successfully; new num_succes_reqs_: 14
00:16:49.058 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 15:0 finished successfully; new num_succes_reqs_: 15
00:16:49.058 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 18:0 finished successfully; new num_succes_reqs_: 16
00:16:49.058 VER-1 - driver_renderer.cpp :91, _renderer_handle_RequestWillBeSent() ::   driver= 1: request 18:1
00:16:49.184 VER-1 - driver_renderer.cpp :124, _renderer_handle_RequestFinished() ::   driver= 1: request 19:0 finished successfully; new num_succes_reqs_: 17
00:16:49.418 INFO  - driver.cpp :177, _on_grace_period_timer_fired() ::   driver= 1: done grace period
00:16:49.418 INFO  - driver_renderer.cpp :162, _report_result() ::   loadnum= 1, webmode= vanilla, proxyMode= tproxy: loadResult= OK: startSec= 1002 plt= 4326 page= [testpage] ttfb= 421 numReqs= 19 numSuccess= 17 numFailed= 0 numAfterDOMLoadEvent= 11
```