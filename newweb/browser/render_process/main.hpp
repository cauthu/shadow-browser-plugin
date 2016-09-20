#ifndef main_hpp
#define main_hpp

#include <string>


void
io_service_send_request_resource(const int& req_id,
                            const char* host,
                            const uint16_t& port,
                            const size_t& req_total_size,
                            const size_t& resp_meta_size,
                            const size_t& resp_body_size);



/* to send renderer ipc messages to client */
void
renderer_send_Loaded();

#endif /* main_hpp */
