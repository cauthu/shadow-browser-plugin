## major note

we want to have a single code base that works both with shadow and as
a native application, and since shadow is more restrictive, we will
develop for shadow, the common denominator.

in what ways is shadow more restrictive? shadow emulates the system
calls an application makes, e.g., `open()`, `socket()`, `read()`,
`write()`, etc. and threading library calls, too. however, we have to
be very careful which ones we use, due to potential bugs in shadow's
emulation or lack of support all together, or certain combinations of
arguments (e.g., it supports `setsockopt()` but not all possible
options).

therefore, we can't use open-source libraries like facebook's `folly`
(which provides some nice async io abstraction wrapped around
`libevent` -- in fact even `libevent`'s `bufferevent` didn't work due
to a bug in shadow's `readv()`/`writev()` emulation.

next is kind of a thought dump; a collection of notes on various
designs, rationales, idiosynacracies, inconsistencies, etc.


### single-threaded

* *we are single-threaded*: because shadow's threading implementation
   is not complete / mature

* which means we have to use a multi-processing model, with a separate
  process that handles IO for the browser (`io_process`) and another
  one for the web/rendering engine (`render_process`)


### event framework / async io

* we use `libevent` and its `evbuffer`

* `bufferevent` wraps a socket fd and can be convenient, but in some
  cases we want raw access to the the socket and do our own buffering,
  especially when implementing a buflo channel, but also when we want
  to optimize by reducing copying: in shadow we run thousands of
  browsers/webservers, and those applications don't care about the
  body content of http requests/responses, so instead of reading them
  into an `evbuffer`, we can read from socket directly into a shared
  "dummy" array. all we care about is how many bytes are
  read/written. these are the `drop_future_input()` and
  `write_dummy()` apis of `StreamChannel`. we could try to optimize
  even further: right now we still take a hit when shadow's
  `read()'/`write()` calls copy the bytes; we could modify those calls
  or introduce new calls to eliminate even more copies

### objects and destruction

* we have a generic `Object` that classes can derive and get a unique
  identifier `objId`. this unique id is useful for logging and also
  for other purposes as well such as a server identifying a client by
  the id of its streamchannel

* when an object calls some callback, the callback might decide to
  free/destroy the object. this will require care to make sure things
  don't go crazy. but we use facebook's folly library's
  `DelayedDestruction`: a class derives from `DelayedDestruction` and
  makes its destructor private will prevent others from calling
  `delete obj`. before calling callbacks, the object instantiates
  `DestructorGuard dg(this)` to protect itself. such objects can be
  freed by calling its `obj->destroy()` method instead of `delete obj`

* so far in our code, we don't actually use `destroy()` because we
  have always used `unique/shared_ptr` with `DelayedDestruction`'s
  `Destructor` to have things taken care of automatically

### callback styles

* we use multiple callback styles in this code base

* sometimes we use an "observer interface" style to do callbacks,
  e.g., a `StreamChannel` notifies its `StreamChannelObserver` of
  events happening on the stream like availability of new data, or the
  stream is closed

* so a class that wants to be notified by such activities by
  `StreamChannel` will need to implement the `StreamChannelObserver`
  interface and set itself as the observer of the stream (other terms
  for "observer" can be "user" or "client")

* another possible style is for the `StreamChannel` to accept callback
  functions, e.g., `std::function` or `boost::function`. then a user
  of the `StreamChannel` just provides the callbacks

* each style has its advantages; just use whichever makes sense

### IPC/RPC

* custom IPC/RPC based on google's `flatbuffers` (much lighter than
  `protobufs`, also lighter than `json` serialization)

* each process provides/implements an "ipc interface", including
  messages that are valid for that interfaces. these interfaces are in
  `utility/ipc/`.

* the `generic_message_channel` is just a abstract channel that uses a
  `stream_channel` (e.g., a `tcp_stream`) that transfers opaque
  ("generic") blocks of bytes ("messages"), which can have a `type`
  (integral value, but again, opaque: i.e., the channel doesn't know
  the semantics of type) and an `id` (of integral type)

* the `generic_ipc_channel` sits on top of a `generic_message_channel`
  and provides features:

      * a general/notification message (underneath it is one without
        an `id`)

      * a `call` message is a message where the user wants a
        response. all this can be implemented with general
        notifications, but the `call()` api of the
        `generic_ipc_channel` is more convenient: the user can specify
        the expected type of the return/reply/response message,
        provide a callback when the response message is received, or
        times out. underneath, the channel specifies a message/call
        `id` when it sends the message to the other end.

      * the other end will be notified of the call with the id, and
        when it replies by calling `reply()` it will include the `id`
        so that the channel can link this message as response to
        earlier call.

### logging

* because shadow has trouble dealing with c++ static variables, we
  can't use logging libraries like boost logging or google glog (which
  use singleton patterns)

* instead, we're using `easyloggingc++`; still have to work around a
  few static variable issues, but it's managable
