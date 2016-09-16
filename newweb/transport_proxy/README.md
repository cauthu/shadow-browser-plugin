

client                                                                          server

client  ------  CSP (ClientSideProxy)   ---------  SSP (ServerSideProxy) ------ server

client                                                                          server




the CSP and SSP call the buflo stream "inner", and their connections
to the client and server, respectively, "outer".

the CSP: uses a ClientHandler to maintaim a single client--server
pair, i.e., it has a TCPChannel from the client, and a buflo stream
towards the server. for the actual forwarding of the bytes, it hands
the two sides to InnerOuterHandler

on SSP: since the ssp needs to support multiple CSPs concurrently, it
has one additional layer of indirection:

* when it gets a new connection, it hands the connection, that
connection must be from a CSP, so it hands the connection to a
CSPHandler

* the CSPHandler takes the connection and establishes the buflo mux
  channel

* when buflo mux channel notifies CSPHandler of a new stream connect
  request, the CSPHandler hands off the request to a StreamHandler

* StreamHandler takes care of connecting to the target server

* when it's connected to the target server, it has an "outer"
  connection, so it tells the inner (bulfo) stream that it succeeds,
  and hands off the inner stream and outer connection to
  InnerOuterHandler

