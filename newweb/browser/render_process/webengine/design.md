(accurate as of chrome 38.0.2125.122)


A web resource is a remote object like an image, a script,
etc. typically identified by a URI

multiple elements on the same page can refer to same resource, e.g.,
multiple "<img>" elements with the same "src" attr

an HTML page is an `HTMLDocument`, a subclass of `Document`. a page
might contain iframes, and each iframe is typically another
`Document`. thus a page has a _main_ document and possibly many child
documents.

each `Document` has a `ResourceFetcher`, from which the document
requests Resources, e.g.:

```
sharedptr<Resource> resource = fetcher->fetch( <ResourceRequest> )
resource->add_client/add_observer(this)
```

where `ResourceRequest` contains things like the URI, headers, desired
timeout, whether-to-bypass-the-cache, etc.

technically the above in some cases does not actually start loading
resource object, and there's a separate function `Resource::load()` to
actually starts loading the resource. for example, a css font resource
can be created when the css is parsed, but webkit won't start loading
the font resource until a later time when some script creates/updates
some element that results in the style of the element requiring the
font.

as the data for the resource comes in, the resource will save the data
in a buffer, and for certain resource types, (e.g., `RawResource` used
for html of main document) will also notify all its clients/observers
via an api such as.

```
virtual void dataReceived(Resource*, const char* data, int length)
```

this is because, for example, the html can be parsed
incrementally. instead of having to wait for the whole html file to be
received.

for us, we don't care about the actual data content, only the progress
in number of bytes, etc.

when the resource has finished loading, it will notify all its client.

note that after the resource has finished downloaded, another client
(e.g., a second `<img>` element that refers to the same URI) can
request it and add itself as client/obserer of resource. in this case,
the resource should have a way to let the client know its current
progress and/or whether it is already finished.


if the server responds with a redirect, then the io process notifies
the renderer's `ResourceLoader`, which asks the `ResourceFetcher` (its
`m_host`) to decide whether to follow the redirect or not and cancel
if not.





### class Page

* `load( <pagemodel> )`

starts loading the main resource:
```
sharedptr<Resource> resource = fetcher->fetch(<main resource instNum>)
resource->add_client/add_observer(this)
```

the model for the html document has a list of
"end-byte-offset"-to-"element" pairs, sorted in increasing order by
their the end-byte-offset.

as the main resource notifies of more bytes:

- if the `HTMLDocumentParser` is not blocked waiting for a script,
  then pump the parser: which will create elements based on this list
  of end-byte-offset-to-element mapping. the parser has to maintain a
  pair of `number of bytes parsed` and `number of bytes avail`: the
  latter is incremented as more bytes come in, and the former is
  incremented as the parser progresses through parsing. the former
  cannot exceed the latter.

- if the parser IS blocked, then do the preloadscanning. the preload
  scanner creates resources and starts loading them but do not create
  elements. this means that later on when the parser gets to parse up
  to this point, it creates corresponding element that will point to a
  resource that is being (pre)loaded or already finished loading


#### `EventTarget`

- is super class of Document, Element, Node, XMLHttpRequest, etc.

- each `EventTarget` has a list of event listeners: e.g., when an
  element has finished loading, it will notify all listeners that have
  registered interest in being notified when it's loaded.

- other events include `DOMContentLoaded` for the Document (i.e., when
  the html has finished downloading and parsing, even though there
  might be embedded resources still downloading),

=============

ResourceFetcher:

* fetch(resInstNum):

  the fetcher will send a request to ioservice --> how does fetcher
  know what params to send to io service? probably look up in the
  model


* as data comes in:

  loop over the list of observers and notify each observer: "here are
  X more bytes available".

  the resource also sticks around, because later on other code might
  try to fetch the same resource, so it can immediately notify the
  user of its current status, e.g., how many bytes it has received or
  whether it has finished , etc.
