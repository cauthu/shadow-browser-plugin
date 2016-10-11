this experiment tests issue #8.

given a small limit of 2 seconds, during each page load, the ssp
should auto-stops multiple times, and the csp should ask ssp to start
again

e.g.

```
me ~/shadow-plugin-extras/newweb/experiments/issue8 > egrep --color "(stop|start again)" shadow.data/hosts/webclient1/stdout-tproxy-1002.log
00:16:44 WARN  - buflo_mux_channel_impl_spdy.cpp :1120, _handle_input_cell() ::   buflomux= 9: SSP has auto-stopped its defense
00:16:44 INFO  - buflo_mux_channel_impl_spdy.cpp :1127, _handle_input_cell() ::   buflomux= 9: ask SSP to start again
00:16:46 WARN  - buflo_mux_channel_impl_spdy.cpp :1120, _handle_input_cell() ::   buflomux= 9: SSP has auto-stopped its defense
00:16:46 INFO  - buflo_mux_channel_impl_spdy.cpp :1127, _handle_input_cell() ::   buflomux= 9: ask SSP to start again
00:16:48 WARN  - buflo_mux_channel_impl_spdy.cpp :1120, _handle_input_cell() ::   buflomux= 9: SSP has auto-stopped its defense
00:16:48 INFO  - buflo_mux_channel_impl_spdy.cpp :1127, _handle_input_cell() ::   buflomux= 9: ask SSP to start again
00:16:50 WARN  - buflo_mux_channel_impl_spdy.cpp :1120, _handle_input_cell() ::   buflomux= 9: SSP has auto-stopped its defense
00:16:50 INFO  - buflo_mux_channel_impl_spdy.cpp :1127, _handle_input_cell() ::   buflomux= 9: ask SSP to start again
00:16:52 WARN  - buflo_mux_channel_impl_spdy.cpp :1120, _handle_input_cell() ::   buflomux= 9: SSP has auto-stopped its defense
00:16:52 INFO  - buflo_mux_channel_impl_spdy.cpp :1127, _handle_input_cell() ::   buflomux= 9: ask SSP to start again
00:16:53 INFO  - ipc.cpp :135, _handle_StopDefense() ::   ipcserv= 4: request to stop buflo defense
```
